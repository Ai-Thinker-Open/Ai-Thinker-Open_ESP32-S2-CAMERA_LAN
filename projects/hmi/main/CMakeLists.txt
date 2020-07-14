set(COMPONENT_SRCS "main.c"
                   "app_wifi_smartconfig.c" 
                   "app_httpserver.c" 
                   "my_symbol.c"
                   "logo.c")
set(COMPONENT_ADD_INCLUDEDIRS "include")

#set(COMPONENT_EMBED_FILES
#        "www/index_ov2640.html.gz"
#        "www/index_ov3660.html.gz"
#        "www/index_ov5640.html.gz"
#        "www/monitor.html.gz")

register_component()
