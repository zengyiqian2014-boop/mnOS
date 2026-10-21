/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    gtk_init(NULL,NULL);
    if(!strcmp(argv[1],"--clipboard")) {
        gchar *value=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        int result=value && !strcmp(value,argv[2])?0:1;
        g_free(value);
        if(result)g_printerr("Search entry did not receive the expected keyboard input\n");
        return result;
    }
    GdkWindow *window=gdk_x11_window_foreign_new_for_display(gdk_display_get_default(),strtoul(argv[1],NULL,10));
    if(!window)return 1;
    if(gdk_window_get_width(window)<400 || gdk_window_get_height(window)<200)return 1;
    GdkPixbuf *image=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));
    if(!image)return 1;
    /* Reject a blank/flat frame; the saved PNG is also visually reviewed. */
    const guchar *data=gdk_pixbuf_get_pixels(image);
    const int stride=gdk_pixbuf_get_rowstride(image),channels=gdk_pixbuf_get_n_channels(image);
    size_t light=0,dark=0;
    for(int y=0;y<gdk_pixbuf_get_height(image);y++)for(int x=0;x<gdk_pixbuf_get_width(image);x++) {
        const guchar *p=data+y*stride+x*channels;
        int v=p[0]+p[1]+p[2];
        if(v>600)light++;
        if(v<420)dark++;
    }
    if(light<100 || dark<100)return 1;
    GError *error=NULL;
    gboolean saved=gdk_pixbuf_save(image,argv[2],"png",&error,NULL);
    if(error){g_printerr("%s\n",error->message);g_error_free(error);}
    g_object_unref(image);
    g_object_unref(window);
    return saved?0:1;
}
