from zeroconf import ServiceInfo, Zeroconf
import socket
    
service_type = "_echosrv._udp.local."
service_name = "udp_echo_ins." + service_type
service_port = 60001
service_text = {"board": "ESP32", "desc": "hello world", 'id':"56781234"}

if __name__ == '__main__':
    # ip_addr = []
 
    # addresses = socket.getaddrinfo(socket.gethostname(), None) # addr[0] 地址族，addr[1] Socket 类型，addr[2] 协议，addr[4] 地址    
    # for address in addresses:
    #    if address[0] == socket.AF_INET: # ipV4
    #        ip_addr.append(socket.inet_aton(address[4][0]))
    #    else: # ipV6
    #        ip_addr.append(socket.inet_pton(socket.AF_INET6, address[4][0]))
            
    ip_addr = [socket.inet_aton('192.168.14.28'), socket.inet_pton(socket.AF_INET6, 'fe80::70eb:c88b:3d3f:5343')]
        
    srv_info = ServiceInfo(
        service_type,
        service_name,
        addresses=ip_addr,
        port=service_port,
        properties=service_text,
    )

    zeroconf = Zeroconf()
    zeroconf.register_service(srv_info)
    print(f"Service {service_name} registered on port {service_port}")
    input("Press enter to exit...\n")

