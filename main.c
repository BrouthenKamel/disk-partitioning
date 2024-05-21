#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>

// Define the GPT header structure
struct {
    unsigned char part1[24];
    unsigned char lba_address[8];
    unsigned char lba_address_copy[8];
    unsigned char data_lba_start[8];
    unsigned char data_lba_end[8];
    unsigned char guid[16];
    unsigned char lba_tp_address[8];
    unsigned char tp_entry_count[4];
    unsigned char tp_entry_size[4];
    unsigned char part3[424];
} gpt_header;

// Define the partition entry structure
struct {
    struct {
        unsigned char type_guid[16];
        unsigned char unique_guid[16];
        unsigned char start_lba[8];
        unsigned char end_lba[8];
        unsigned char attributes[8];
        unsigned char partition_name[72];
    } partition_entries[4];
} partition_table;

// Define the boot sector structure
struct {
    unsigned char part1[11];
    struct {
        unsigned char part1[2];
        unsigned char reserved_sector_count[2];
        unsigned char fat_count;
        unsigned char part2[5];
        unsigned char fat_sector_count[2];
        unsigned char part3[12];
    } bios;
    unsigned char part2[476];
} boot_sector;

// Function to display the list of disks
void list_disks() {
    struct dirent *de;
    DIR *dr = opendir("/dev");
    if (dr == NULL) {
        printf("Could not open current directory /dev\n");
        return;
    }
    
    bool disks_found = false;
    while ((de = readdir(dr)) != NULL) {
        // Check if the directory entry name starts with 'sd' and is 3 characters long
        if (de->d_name[0] == 's' && de->d_name[1] == 'd' && strlen(de->d_name) == 3) {
            disks_found = true;
            printf("%s\n", de->d_name);
        }
    }
    closedir(dr);
    
    if (!disks_found) {
        printf("No disk found in this machine\n");
    }
    return;
}

// Function to display disk information
void display_disk_info(char *disk_name) {
    FILE* disk;
    char path[255] = "";
    int num_read;
    struct stat buffer;

    strcat(path, "/dev/");
    strcat(path, disk_name);

    printf("\nDisk path: %s \n", path);
    
    // Check if the file exists and is a block device
    if (stat(path, &buffer) != 0) {
        printf("\nERROR: Disk does not exist or is not accessible\n");
        exit(EXIT_FAILURE);
    }

    disk = fopen(path, "rb");
    if (disk == NULL) {
        printf("\nERROR: Unable to open disk\n");
        exit(0);
    }

    int err = fseek(disk, 512, SEEK_SET);
    if (err != 0) {
        printf("\nERROR: Failed to seek on the disk\n");
        exit(0);
    }

    num_read = fread(&gpt_header, 512, 1, disk);
    if (num_read <= 0) {
        printf("\nERROR reading GPT header\n");
        exit(0);
    }

    int partition_table_address = *(int *) &(gpt_header.lba_tp_address);
    printf("\n***** LBA address of the partition table: %d\n", partition_table_address);

    int data_start_address = *(int *) &(gpt_header.data_lba_start);
    printf("\n***** LBA start address of the data area: %d\n", data_start_address);

    int data_end_address = *(int *) &(gpt_header.data_lba_end);
    printf("\n***** LBA end address of the data area: %d\n", data_end_address);
}

// Function to calculate the LBA address of the root cluster
void calculate_cluster_address(int address, FILE *disk) {
    printf("\n\n**** LBA addresses ****\n");

    if (disk == NULL) {
        printf("\nERROR: Unable to open disk\n");
        exit(0);
    }

    printf("\n**** LBA address of the root cluster: %d\n", address);
    int address_offset = 512 * address;

    int err = fseek(disk, address_offset, SEEK_SET);
    if (err != 0) {
        printf("\nERROR: Failed to seek on the disk\n");
        exit(0);
    }

    int num_read = fread(&boot_sector, 512, 1, disk);
    if (num_read <= 0) {
        printf("\nERROR reading boot sector\n");
        exit(0);
    }

    int reserved_sector_count = *(int *) &(boot_sector.bios.reserved_sector_count);
    int fat_count = *(int *) &(boot_sector.bios.fat_count);
    int fat_sector_count = *(int *) &(boot_sector.bios.fat_sector_count);

    int cluster_address = address + reserved_sector_count + (fat_count * fat_sector_count);
    printf("\n**** LBA address of the first cluster: %d\n", cluster_address);
}

// Function to display partition information
void display_partition_info(char *disk_name) {
    FILE* disk;
    char path[255] = "";
    int num_read;
    
    strcat(path, "/dev/");
    strcat(path, disk_name);

    disk = fopen(path, "rb");
    if (disk == NULL) {
        printf("\nERROR: Unable to open disk\n");
        exit(0);
    }

    int err = fseek(disk, 512 * 2, SEEK_SET);
    if (err != 0) {
        printf("\nERROR: Failed to seek on the disk\n");
        exit(0);
    }

    num_read = fread(&partition_table, 512, 1, disk);
    if (num_read <= 0) {
        printf("\nERROR reading partition table\n");
        exit(0);
    }

    printf("\n**** Partition Table ****\n");
    printf("\n|-----------|----------------|----------------|----------------|---------------|");
    printf("\n| Partition |   LBA_START    |    LBA_END     | Sector Count   | Size in GB    |");
    printf("\n|-----------|----------------|----------------|----------------|---------------|");

    int gigabyte = 1024 * 1024 * 1024;
    for (int i = 0; i < 4; i++) {
        int start_lba = *(int *) &(partition_table.partition_entries[i].start_lba);
        int end_lba = *(int *) &(partition_table.partition_entries[i].end_lba);
        int sector_count = end_lba - start_lba;
        double size_in_bytes = (double) sector_count * 512;
        double size_in_gb = size_in_bytes / gigabyte;

        printf("\n|sdb%d       |  0x'%8d'  |  0x'%8d'  |  %10d    |%10.3f     |", i, start_lba, end_lba, sector_count, size_in_gb);
        printf("\n|-----------|----------------|----------------|----------------|---------------|");
    }

    calculate_cluster_address(*(int *) &(partition_table.partition_entries[1].start_lba), disk);
}

// Main function
int main() {
    // Display the list of disks
    printf("--------- List of Disks ---------\n");
    list_disks();

    // Prompt the user to select a disk
    printf("--------- Select a Disk ---------\n");
    char disk[10];
    printf("=> Enter the physical disk name: ");
    scanf("%9s", disk);

    // Display disk information
    display_disk_info(disk);

    // Display partition information
    display_partition_info(disk);

    return 0;
}
