from zeroconf import IPVersion, Zeroconf

srv_type = '_echo_srv'
ins_name = 'udp_echo_ins'

if __name__ == '__main__':
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
    srv_infos = zeroconf.get_service_info(srv_type, ins_name, 3000)
    for srv_info in srv_infos.values():
        print(srv_info)
    zeroconf.close()
