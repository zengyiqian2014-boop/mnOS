// SPDX-License-Identifier: GPL-2.0-only
/* mnOS guest-side SBI devices. This file belongs to the Linux guest, not the
 * original mnOS host kernel. ABI is documented in include/guest_devices.h. */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/input.h>
#include <linux/hw_random.h>
#include <linux/workqueue.h>
#include <linux/timekeeping.h>
#include <asm/sbi.h>
#include <asm/io.h>

enum { GD_INFO, GD_READ, GD_WRITE, GD_SIZE, GD_SYNC, GD_RX, GD_TX, GD_MAC, GD_INPUT, GD_RANDOM, GD_TIME };
static struct sbiret call(unsigned fn,unsigned long a,unsigned long b,unsigned long c,unsigned long d)
{
    return sbi_ecall(0x09004d4e,fn,a,b,c,d,0,0);
}
static unsigned char *disk_buffer;
static unsigned disk_io_max;
static DEFINE_SPINLOCK(disk_lock);
static void disk_submit(struct bio *bio)
{
    unsigned disk=(unsigned)(uintptr_t)bio->bi_bdev->bd_disk->private_data;
    unsigned op=bio_op(bio),size=bio->bi_iter.bi_size,at=0;
    struct bio_vec bvec;struct bvec_iter iter;
    u64 offset=(u64)bio->bi_iter.bi_sector*512;
    unsigned long flags;
    spin_lock_irqsave(&disk_lock,flags);
    if(op==REQ_OP_FLUSH){if(call(GD_SYNC,disk,0,0,0).error)goto fail;goto done;}
    if(op!=REQ_OP_READ&&op!=REQ_OP_WRITE)goto fail;
    if(!size||size>disk_io_max||(size&511))goto fail;
    if((bio->bi_opf&REQ_PREFLUSH)&&call(GD_SYNC,disk,0,0,0).error)goto fail;
    /* Gather the whole bio before one SBI call. Completing each 4 KiB page
     * separately forces a full host filesystem transaction per page. The
     * bounded, physically contiguous bounce buffer also handles nonadjacent
     * Linux pages without exposing their host addresses. */
    bio_for_each_segment(bvec,bio,iter) {
        unsigned n=bvec.bv_len;
        void *page;
        if(n>PAGE_SIZE||(n&511)||(bvec.bv_offset&511)||bvec.bv_offset>PAGE_SIZE-n||n>size-at)goto fail;
        if(op==REQ_OP_WRITE) {
            page=kmap_local_page(bvec.bv_page);
            memcpy(disk_buffer+at,page+bvec.bv_offset,n);
            kunmap_local(page);
        }
        at+=n;
    }
    if(at!=size||call(op==REQ_OP_READ?GD_READ:GD_WRITE,disk,offset,__pa(disk_buffer),size).error)goto fail;
    if(op==REQ_OP_READ) {
        at=0;
        bio_for_each_segment(bvec,bio,iter) {
            void *page=kmap_local_page(bvec.bv_page);
            memcpy(page+bvec.bv_offset,disk_buffer+at,bvec.bv_len);
            kunmap_local(page);at+=bvec.bv_len;
        }
    }
    if((bio->bi_opf&REQ_FUA)&&call(GD_SYNC,disk,0,0,0).error)goto fail;
done:spin_unlock_irqrestore(&disk_lock,flags);bio_endio(bio);return;
fail:spin_unlock_irqrestore(&disk_lock,flags);bio_io_error(bio);
}
static const struct block_device_operations disk_ops={.owner=THIS_MODULE,.submit_bio=disk_submit};
static int disks_init(unsigned features)
{
    int major=register_blkdev(0,"mnos");unsigned i;
    if(major<0)return major;
    disk_io_max=(features&128)?128*1024:4096;
    disk_buffer=(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO,get_order(disk_io_max));
    if(!disk_buffer){unregister_blkdev(major,"mnos");return -ENOMEM;}
    for(i=0;i<2;i++) {
        struct queue_limits lim={.logical_block_size=512,.physical_block_size=4096,
            .max_hw_sectors=disk_io_max/512,.max_segment_size=PAGE_SIZE,
            .max_segments=disk_io_max/PAGE_SIZE,.features=BLK_FEAT_SYNCHRONOUS};
        struct sbiret size=call(GD_SIZE,i,0,0,0);struct gendisk *disk;int error;
        if(size.error||!size.value)continue;
        disk=blk_alloc_disk(&lim,NUMA_NO_NODE);if(IS_ERR(disk))return PTR_ERR(disk);
        disk->major=major;disk->first_minor=i;disk->minors=1;disk->fops=&disk_ops;
        disk->private_data=(void *)(uintptr_t)i;
        snprintf(disk->disk_name,DISK_NAME_LEN,"mnos%u",i);set_capacity(disk,size.value);
        if(!i)set_disk_ro(disk,true);
        error=add_disk(disk);if(error){put_disk(disk);return error;}
    }
    pr_info("mnOS disks: maximum synchronous transfer %u KiB\n",disk_io_max/1024);
    return 0;
}
static struct net_device *nic;
static struct input_dev *keyboard,*pointer;
static struct delayed_work poll_work;
static unsigned char *tx_buffer,*rx_buffer,*entropy_buffer;
static DEFINE_SPINLOCK(tx_lock);
static int net_open(struct net_device *dev){netif_start_queue(dev);return 0;}
static int net_stop(struct net_device *dev){netif_stop_queue(dev);return 0;}
static netdev_tx_t net_tx(struct sk_buff *skb,struct net_device *dev)
{
    struct sbiret sent;unsigned long flags;
    if(skb->len>1514){dev->stats.tx_dropped++;dev_kfree_skb(skb);return NETDEV_TX_OK;}
    spin_lock_irqsave(&tx_lock,flags);
    if(skb_copy_bits(skb,0,tx_buffer,skb->len)){spin_unlock_irqrestore(&tx_lock,flags);dev_kfree_skb(skb);return NETDEV_TX_OK;}
    sent=call(GD_TX,__pa(tx_buffer),skb->len,0,0);spin_unlock_irqrestore(&tx_lock,flags);
    if(!sent.error&&!sent.value)return NETDEV_TX_BUSY;
    if(sent.error)dev->stats.tx_errors++;else{dev->stats.tx_packets++;dev->stats.tx_bytes+=skb->len;}
    dev_kfree_skb(skb);return NETDEV_TX_OK;
}
static const struct net_device_ops net_ops={.ndo_open=net_open,.ndo_stop=net_stop,.ndo_start_xmit=net_tx,.ndo_validate_addr=eth_validate_addr};
static int net_init(void)
{
    struct sbiret address=call(GD_MAC,0,0,0,0);u8 mac[ETH_ALEN];unsigned i;int error;
    if(address.error)return -ENODEV;
    for(i=0;i<ETH_ALEN;i++)mac[i]=(address.value>>(i*8))&255;
    if(!is_valid_ether_addr(mac))return -EINVAL;
    nic=alloc_etherdev(0);if(!nic)return -ENOMEM;
    strscpy(nic->name,"eth%d",IFNAMSIZ);nic->netdev_ops=&net_ops;eth_hw_addr_set(nic,mac);
    nic->min_mtu=68;nic->max_mtu=1500;
    tx_buffer=(void *)get_zeroed_page(GFP_KERNEL);rx_buffer=(void *)get_zeroed_page(GFP_KERNEL);
    if(!tx_buffer||!rx_buffer){free_netdev(nic);nic=NULL;return -ENOMEM;}
    error=register_netdev(nic);if(error){free_netdev(nic);nic=NULL;return error;}
    netif_carrier_on(nic);return 0;
}
static int input_init(void)
{
    unsigned i;int error;
    keyboard=input_allocate_device();pointer=input_allocate_device();
    if(!keyboard||!pointer)return -ENOMEM;
    keyboard->name="mnOS guest keyboard";keyboard->id.bustype=BUS_VIRTUAL;
    /* BTN_* shares the key namespace. Advertising every KEY_MAX code would
     * make Xorg classify this keyboard as an additional mouse. */
    for(i=1;i<256;i++)input_set_capability(keyboard,EV_KEY,i);
    error=input_register_device(keyboard);if(error)return error;
    pointer->name="mnOS guest pointer";pointer->id.bustype=BUS_VIRTUAL;
    input_set_abs_params(pointer,ABS_X,0,1023,0,0);input_set_abs_params(pointer,ABS_Y,0,767,0,0);
    input_set_capability(pointer,EV_KEY,BTN_LEFT);input_set_capability(pointer,EV_KEY,BTN_RIGHT);
    error=input_register_device(pointer);return error;
}
static void poll_devices(struct work_struct *work)
{
    unsigned budget=32;(void)work;
    while(nic&&netif_running(nic)&&budget--) {
        struct sbiret received=call(GD_RX,__pa(rx_buffer),1514,0,0);struct sk_buff *skb;
        if(received.error||received.value<=0||received.value>1514)break;
        skb=netdev_alloc_skb_ip_align(nic,received.value);if(!skb){nic->stats.rx_dropped++;continue;}
        skb_put_data(skb,rx_buffer,received.value);skb->protocol=eth_type_trans(skb,nic);
        nic->stats.rx_packets++;nic->stats.rx_bytes+=received.value;netif_rx(skb);
    }
    /* Yield between short input batches. Draining the entire host ring before
     * Xorg can read evdev would overflow its per-client queue and cause
     * SYN_DROPPED, losing fast typing/pastes despite valid SBI events. */
    budget=16;
    while(keyboard&&pointer&&budget--) {
        struct sbiret r=call(GD_INPUT,0,0,0,0);u64 e=r.value;unsigned type=(e>>48)&0x7fff,code=(e>>32)&65535;
        if(r.error||!(e>>63))break;
        if(type==EV_SYN){input_sync(keyboard);input_sync(pointer);}
        else input_event(type==EV_ABS||(type==EV_KEY&&code>=BTN_MOUSE)?pointer:keyboard,type,code,(s32)e);
    }
    schedule_delayed_work(&poll_work,msecs_to_jiffies(10));
}
static int entropy_read(struct hwrng *rng,void *out,size_t max,bool wait)
{
    size_t n=min_t(size_t,max,PAGE_SIZE);(void)rng;(void)wait;
    if(call(GD_RANDOM,__pa(entropy_buffer),n,0,0).error)return -EIO;
    memcpy(out,entropy_buffer,n);memzero_explicit(entropy_buffer,n);return n;
}
static struct hwrng rng={.name="mnos-host-csprng",.read=entropy_read,.quality=1024};
static int __init mnos_init(void)
{
    struct sbiret features=call(GD_INFO,0,0,0,0),now;int error;
    if(features.error)return -ENODEV;
    error=disks_init(features.value);if(error)return error;
    if(features.value&2){error=net_init();if(error)pr_err("mnos network: %d\n",error);}
    if(features.value&8){entropy_buffer=(void *)get_zeroed_page(GFP_KERNEL);if(entropy_buffer){error=hwrng_register(&rng);if(error)pr_err("mnos rng: %d\n",error);}}
    now=call(GD_TIME,0,0,0,0);
    if(!now.error&&now.value>0){struct timespec64 time={.tv_sec=now.value};do_settimeofday64(&time);}
    error=input_init();if(error)return error;
    INIT_DELAYED_WORK(&poll_work,poll_devices);schedule_delayed_work(&poll_work,1);
    pr_info("mnOS SBI devices: disks, input, network=%d\n",!!(features.value&2));return 0;
}
device_initcall(mnos_init);
MODULE_LICENSE("GPL");
