from zeroconf import IPVersion, ServiceInfo, Zeroconf

srv_type = '_echo_srv'
ins_name = 'udp_echo_ins'
hostname = 'abcd1234'
ip = '192.168.1.101'
port = 60001
txt_items = {
    'board':'ESP32',
    'id':'udp echo service'
}

if __name__ == '__main__':
    srv_info = ServiceInfo(srv_type, ins_name, port, properties=txt_items, addresses=[ip], server=hostname)
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
    zeroconf.register_service(srv_info)
