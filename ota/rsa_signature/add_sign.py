#!/usr/bin/python
import os
# pip install pycryptodome
from Crypto.PublicKey import RSA  
from Crypto.Signature import pkcs1_15  
from Crypto.Hash import SHA256  


pubkey_file = 'main/certs/sign_pub.key'
privkey_file = 'sign_priv.key'
src_file = 'build/https_ota.bin'
output_file = 'https_ota_sign.bin'


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

        input_size = os.path.getsize(src_file)
        output_size = os.path.getsize(output_file)
        print("src_file: " + src_file)
        print("src_file: length = %d" %(input_size))
        print("output_file: " + output_file)
        print("output_file: length = %d" %(output_size))
        print('add signature ok')
    else:
        print("file not exists: " + src_file)
