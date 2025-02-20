from zeroconf import Zeroconf, ServiceBrowser, ServiceStateChange
import socket

def on_service_state_change(zeroconf, service_type, name, state_change):
    if state_change is ServiceStateChange.Added:
        print(f"Service {name} added")
        info = zeroconf.get_service_info(service_type, name)
        if info:
            print(f"  Address: {socket.inet_ntoa(info.addresses[0])}")
            print(f"  Port: {info.port}")
            print(f"  PTR: {name}")
            print(f"  SRV: {info.server}")
            print(f"  TXT: {info.properties}")
    elif state_change is ServiceStateChange.Removed:
        print(f"Service {name} removed")

service_type = "_echosrv._udp.local."

if __name__ == '__main__':
    zeroconf = Zeroconf()
    browser = ServiceBrowser(zeroconf, service_type, handlers=[on_service_state_change])
    print(f"Searching for {service_type} services...")
    input("Press enter to exit...\n")
