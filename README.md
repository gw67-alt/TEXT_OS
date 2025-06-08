cin >> input, acts as getline, perhaps an oversimplification...

limitations are: cin cannot directly accept int, use atoi

TODO: streamline driver commands in txt files with implemented sata driver(stored elsewhere), possibly use DMA

port_addr = ahci_base + 0x100 + (port * 0x80);
