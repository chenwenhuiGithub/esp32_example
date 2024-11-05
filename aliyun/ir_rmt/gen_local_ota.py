#!/usr/bin/python
import os
import time
import struct
# pip install pycryptodome
from Crypto.PublicKey import RSA  
from Crypto.Signature import pkcs1_15  
from Crypto.Hash import SHA256  

pubkey_file = 'local_ota_sign_pub.key'
privkey_file = 'local_ota_sign_priv.key'
src_file = 'build/ir_rmt.bin'

def generate_keys():
    key = RSA.generate(2048)
    private_key = key.export_key()
    public_key = key.publickey().export_key()

    with open(privkey_file, 'wb') as f_1:
        f_1.write(private_key)

    with open(pubkey_file, 'wb') as f_2:
        f_2.write(public_key)

def calc_hash(data):
    hash_obj = SHA256.new(data)
    return hash_obj

def calc_signature(privkey, hash):
    signature = pkcs1_15.new(privkey).sign(hash)
    return signature

if __name__ == '__main__':
    if os.path.exists(src_file):
        # generate rsa key pair, only call one time
        # generate_keys()

        with open(src_file, 'rb') as f_1:
            src_data = f_1.read()

        app_version = struct.unpack_from('32s', src_data, 48)[0].decode('utf-8').rstrip('\x00')
        print(app_version)
        output_file = 'ir_rmt_{}_{}.bin'.format(app_version, time.strftime("%Y%m%d%H%M%S"))
        print(output_file)

        hash_data = calc_hash(src_data)
        print('hash:')
        for i in range(0, len(hash_data.digest()), 16):
            line = hash_data.digest()[i:i + 16]
            formatted_line = " ".join(f"{byte:02x}" for byte in line)
            print(formatted_line)

        with open(privkey_file, 'rb') as f_2:
            private_key = RSA.import_key(f_2.read())
        sign_data = calc_signature(private_key, hash_data)
        print('signature:')
        for i in range(0, len(sign_data), 16):
            line = sign_data[i:i + 16]
            formatted_line = " ".join(f"{byte:02x}" for byte in line)
            print(formatted_line)  

        with open(output_file, 'wb') as f_3:
            f_3.write(src_data)
            f_3.write(sign_data)

        print('success')
    else:
        print("failed, file not exists: " + src_file)
