#!/usr/bin/env python

"""
    This program reads in an intel hex file format and converts
    it to a byte array for use in c programs
    It is used to convert a bootloader to a byte array for the 
    bootloader updater program to update the bootloader
    It takes the first parameter as the intel file to process
    and outputs a new_boot.h with the number of pages defined
    and the array.
"""
import sys

def main():
    PAGE_SIZE = 256
    parsed_data = []
    with open(sys.argv[1],'r') as fhex:
        for line in fhex:
            dtype = int(line[7:9],16)
            if dtype != 0:
                continue
            size = int(line[1:3],16)*2
            data = line[9:9+size]
            for i in range(0,size,2):
                parsed_data.append(int(data[i:i+2],16))

    data_length = len(parsed_data)
    add_length = PAGE_SIZE-(data_length % PAGE_SIZE)

    for i in range(add_length):
        parsed_data.append(0xFF)

    num_pages = int(len(parsed_data) / PAGE_SIZE)
    new_data = []
    for i in range(0,len(parsed_data), int(PAGE_SIZE/8)):
        new_data.append(['0x{:02X},'.format(x) for x in parsed_data[i:i+int(PAGE_SIZE/8)]])

    with open('new_boot.h', 'w') as fn:
        fn.write('#define NUMBER_OF_PAGES {pages}\n\n'.format(pages=num_pages))
        fn.write('uint8_t newbootloader[{size}] = {{\n'.format(size=len(parsed_data)))
        for line in new_data:
            fn.write(''.join(line))
            fn.write('\n')
        fn.write('};\n')

if __name__ == '__main__':
    sys.exit(main())
