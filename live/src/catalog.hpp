// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <json-glib/json-glib.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#ifndef MNOS_TEST_CATALOG_URL
static constexpr const char *catalog_url="https://raw.githubusercontent.com/zengyiqian2014-boop/mnOS/resources/catalog.json.asc";
#else
static constexpr const char *catalog_url=MNOS_TEST_CATALOG_URL;
#endif
struct Catalog {
    std::string directory;
    JsonParser *parser=nullptr;
    JsonObject *object=nullptr;
    Catalog() {
        char temp[]="/tmp/mnos-catalog.XXXXXX";
        if(!mkdtemp(temp))throw std::runtime_error("Cannot create resource cache.");
        directory=temp;
        try {
#ifdef MNOS_TEST_CATALOG_URL
            const char *protocol="=file";
#else
            const char *protocol="=https";
#endif
            if(run({"/usr/bin/curl","--disable","--fail","--silent","--show-error","--location",
                "--proto",protocol,"--proto-redir",protocol,"--connect-timeout","15",
                "--max-time","120","--retry","2","--max-filesize","8388608",
                "--output",directory+"/signed",catalog_url}))throw std::runtime_error("Cannot fetch the GitHub resource index; no packages changed.");
            if(run({"/usr/bin/gpgv","--keyring","/usr/share/keyrings/mnos-archive.gpg",
                "--output",directory+"/catalog.json",directory+"/signed"}))
                throw std::runtime_error("Resource index signature failed; no packages changed.");
            parser=json_parser_new();GError *error=nullptr;
            if(!json_parser_load_from_file(parser,(directory+"/catalog.json").c_str(),&error)) {
                if(error)g_error_free(error);
                throw std::runtime_error("Invalid resource index JSON.");
            }
            JsonNode *root=json_parser_get_root(parser);
            if(!root||!JSON_NODE_HOLDS_OBJECT(root))throw std::runtime_error("Invalid resource index.");
            object=json_node_get_object(root);
            if(!json_object_has_member(object,"schema")||json_object_get_int_member(object,"schema")!=1 ||
                !json_object_has_member(object,"resources")||!JSON_NODE_HOLDS_ARRAY(json_object_get_member(object,"resources")))
                throw std::runtime_error("Unsupported resource index schema.");
        } catch(...) {clear();throw;}
    }
    Catalog(const Catalog&)=delete;
    Catalog& operator=(const Catalog&)=delete;
    void clear() {
        if(parser) {g_object_unref(parser);parser=nullptr;}
        if(!directory.empty()) {unlink((directory+"/signed").c_str());unlink((directory+"/catalog.json").c_str());rmdir(directory.c_str());}
    }
    ~Catalog() {clear();}
    static std::string field(JsonObject *o,const char *name) {
        if(!o||!json_object_has_member(o,name))throw std::runtime_error("Incomplete resource index.");
        JsonNode *n=json_object_get_member(o,name);
        if(!JSON_NODE_HOLDS_VALUE(n)||json_node_get_value_type(n)!=G_TYPE_STRING)
            throw std::runtime_error("Invalid resource index field.");
        std::string value=json_node_get_string(n);
        if(value.find_first_of("\r\n\t")!=std::string::npos)throw std::runtime_error("Invalid resource index text.");
        return value;
    }
    std::string latest() const {return field(object,"latest_linux_runtime");}
    int list(const std::string &kind) const {
        auto *entries=json_object_get_array_member(object,"resources");
        std::cout<<"TYPE\tEDITION\tARCH\tVERSION\tNAME\n";
        for(guint i=0;i<json_array_get_length(entries);++i) {
            JsonNode *node=json_array_get_element(entries,i);
            if(!JSON_NODE_HOLDS_OBJECT(node))throw std::runtime_error("Invalid resource entry.");
            auto *entry=json_node_get_object(node);
            auto type=field(entry,"kind");if(kind!="all"&&kind!=type)continue;
            std::cout<<type<<'\t'<<field(entry,"edition")<<'\t'<<field(entry,"architecture")<<'\t'
                <<field(entry,"version")<<'\t'<<field(entry,"name")<<'\n'<<"  "<<field(entry,"url")<<'\n';
        }
        return 0;
    }
};
