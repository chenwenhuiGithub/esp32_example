idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ""
                    EMBED_TXTFILES "certs/mosquitto_cert.pem"
                                   "certs/client_cert.pem"
                                   "certs/client_prvtkey.pem")
