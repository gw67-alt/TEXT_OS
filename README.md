cin >> input, acts as getline, perhaps an oversimplification...

limitations are: cin cannot directly accept int, use atoi

SATA read/write data works on VMware and real hardware.

Filesystem test: make a dir command to list filenames and LBAs for each file(FAT), use read sectors. preserve some bytes in sector for possible end of chain byte. directories use LBA groupings with an end of directory byte.
