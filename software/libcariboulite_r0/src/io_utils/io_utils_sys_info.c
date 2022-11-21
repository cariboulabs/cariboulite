#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_SysInfo"

#include <stdio.h>
#include <arpa/inet.h>
#include <time.h>
#include "zf_log/zf_log.h"
#include "io_utils_sys_info.h"


//=====================================================================
static void io_utils_fill_sys_info(io_utils_sys_info_st *sys_info)
{    
    if (!strcmp(sys_info->processor, "BCM2835"))
    {
        // PI ZERO / PI1
        sys_info->processor_type = io_utils_processor_BCM2835;
        sys_info->phys_reg_base = 0x20000000;
        sys_info->sys_clock_hz = 700000000;
        sys_info->bus_reg_base = 0x7E000000;
    } 
    else if (!strcmp(sys_info->processor, "BCM2836") || !strcmp(sys_info->processor, "BCM2837"))
    {
        // PI2 / PI3
        sys_info->processor_type = io_utils_processor_BCM2836;
        sys_info->phys_reg_base = 0x3F000000;
        sys_info->sys_clock_hz = 600000000;
        sys_info->bus_reg_base = 0x7E000000;
    }
    else if (!strcmp(sys_info->processor, "BCM2711"))
    {
        // PI4 / PI400
        sys_info->processor_type = io_utils_processor_BCM2711;
        sys_info->phys_reg_base = 0xFE000000;
        sys_info->sys_clock_hz = 600000000;
        sys_info->bus_reg_base = 0x7E000000;
    }
    else
    {
        // USE ZERO AS DEFAULT
        sys_info->processor_type = io_utils_processor_UNKNOWN;
        sys_info->phys_reg_base = 0x20000000;
        sys_info->sys_clock_hz = 400000000;
        sys_info->bus_reg_base = 0x7E000000;
    }

    if (!strcmp(sys_info->ram, "256M")) sys_info->ram_size_mbytes = 256;
    else if (!strcmp(sys_info->ram, "512M")) sys_info->ram_size_mbytes = 512;
    else if (!strcmp(sys_info->ram, "1G")) sys_info->ram_size_mbytes = 1000;
    else if (!strcmp(sys_info->ram, "2G")) sys_info->ram_size_mbytes = 2000;
    else if (!strcmp(sys_info->ram, "4G")) sys_info->ram_size_mbytes = 4000;
    else if (!strcmp(sys_info->ram, "8G")) sys_info->ram_size_mbytes = 8000;
}


//=====================================================================
int io_utils_get_rpi_info(io_utils_sys_info_st *info)
{
   FILE *fp;
   char buffer[1024];
   char hardware[1024];
   char revision[1024];
   int found = 0;
   int len;

   if ((fp = fopen("/proc/device-tree/system/linux,revision", "r"))) {
      uint32_t n;
      if (fread(&n, sizeof(n), 1, fp) != 1) 
      {
         fclose(fp);
         return -1;
      }
      sprintf(revision, "%x", ntohl(n));
      found = 1;
   }
   else if ((fp = fopen("/proc/cpuinfo", "r"))) {
      while(!feof(fp) && fgets(buffer, sizeof(buffer), fp)) {
         sscanf(buffer, "Hardware	: %s", hardware);
         if (strcmp(hardware, "BCM2708") == 0 ||
             strcmp(hardware, "BCM2709") == 0 ||
             strcmp(hardware, "BCM2711") == 0 ||
             strcmp(hardware, "BCM2835") == 0 ||
             strcmp(hardware, "BCM2836") == 0 ||
             strcmp(hardware, "BCM2837") == 0 ) {
            found = 1;
         }
         sscanf(buffer, "Revision	: %s", revision);
      }
   }
   else
      return -1;
   fclose(fp);

   if (!found)
      return -1;

   if ((len = strlen(revision)) == 0)
      return -1;

   if (len >= 6 && strtol((char[]){revision[len-6],0}, NULL, 16) & 8) {
      // new scheme
      //info->rev = revision[len-1]-'0';
      strcpy(info->revision, revision);
      switch (revision[len-3]) {
         case '0' :
            switch (revision[len-2]) {
               case '0': info->type = "Model A"; info->p1_revision = 2; break;
               case '1': info->type = "Model B"; info->p1_revision = 2; break;
               case '2': info->type = "Model A+"; info->p1_revision = 3; break;
               case '3': info->type = "Model B+"; info->p1_revision = 3; break;
               case '4': info->type = "Pi 2 Model B"; info->p1_revision = 3; break;
               case '5': info->type = "Alpha"; info->p1_revision = 3; break;
               case '6': info->type = "Compute Module 1"; info->p1_revision = 0; break;
               case '8': info->type = "Pi 3 Model B"; info->p1_revision = 3; break;
               case '9': info->type = "Zero"; info->p1_revision = 3; break;
               case 'a': info->type = "Compute Module 3"; info->p1_revision = 0; break;
               case 'c': info->type = "Zero W"; info->p1_revision = 3; break;
               case 'd': info->type = "Pi 3 Model B+"; info->p1_revision = 3; break;
               case 'e': info->type = "Pi 3 Model A+"; info->p1_revision = 3; break;
               default : info->type = "Unknown"; info->p1_revision = 3; break;
            } break;
         case '1':
            switch (revision[len-2]) {
               case '0': info->type = "Compute Module 3+"; info->p1_revision = 0; break;
               case '1': info->type = "Pi 4 Model B"; info->p1_revision = 3; break;
               case '3': info->type = "Pi 400"; info->p1_revision = 3; break;
               case '4': info->type = "Compute Module 4"; info->p1_revision = 0; break;
               default : info->type = "Unknown"; info->p1_revision = 3; break;
            } break;
         default: info->type = "Unknown"; info->p1_revision = 3; break;
      }

      switch (revision[len-4]) {
         case '0': info->processor = "BCM2835"; break;
         case '1': info->processor = "BCM2836"; break;
         case '2': info->processor = "BCM2837"; break;
         case '3': info->processor = "BCM2711"; break;
         default : info->processor = "Unknown"; break;
      }
      switch (revision[len-5]) {
         case '0': info->manufacturer = "Sony UK"; break;
         case '1': info->manufacturer = "Egoman"; break;
         case '2': info->manufacturer = "Embest"; break;
         case '3': info->manufacturer = "Sony Japan"; break;
         case '4': info->manufacturer = "Embest"; break;
         case '5': info->manufacturer = "Stadium"; break;
         default : info->manufacturer = "Unknown"; break;
      }
      switch (strtol((char[]){revision[len-6],0}, NULL, 16) & 7) {
         case 0: info->ram = "256M"; break;
         case 1: info->ram = "512M"; break;
         case 2: info->ram = "1G"; break;
         case 3: info->ram = "2G"; break;
         case 4: info->ram = "4G"; break;
         case 5: info->ram = "8G"; break;
         default: info->ram = "Unknown"; break;
      }
   } else {
      // old scheme
      info->ram = "Unknown";
      info->manufacturer = "Unknown";
      info->processor = "Unknown";
      info->type = "Unknown";
      strcpy(info->revision, revision);

      uint64_t rev;
      sscanf(revision, "%llx", &rev);
      rev = rev & 0xefffffff;       // ignore preceeding 1000 for overvolt

      if (rev == 0x0002 || rev == 0x0003) {
         info->type = "Model B";
         info->p1_revision = 1;
         info->ram = "256M";
         info->manufacturer = "Egoman";
         info->processor = "BCM2835";
      } else if (rev == 0x0004) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x0005) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Qisda";
         info->processor = "BCM2835";
      } else if (rev == 0x0006) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Egoman";
         info->processor = "BCM2835";
      } else if (rev == 0x0007) {
         info->type = "Model A";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Egoman";
         info->processor = "BCM2835";
      } else if (rev == 0x0008) {
         info->type = "Model A";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x0009) {
         info->type = "Model A";
         info->p1_revision = 2;
         info->ram = "256M";
         info->manufacturer = "Qisda";
         info->processor = "BCM2835";
      } else if (rev == 0x000d) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "512M";
         info->manufacturer = "Egoman";
         info->processor = "BCM2835";
      } else if (rev == 0x000e) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "512M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x000f) {
         info->type = "Model B";
         info->p1_revision = 2;
         info->ram = "512M";
         info->manufacturer = "Qisda";
         info->processor = "BCM2835";
      } else if (rev == 0x0010) {
         info->type = "Model B+";
         info->p1_revision = 3;
         info->ram = "512M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x0011) {
         info->type = "Compute Module 1";
         info->p1_revision = 0;
         info->ram = "512M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x0012) {
         info->type = "Model A+";
         info->p1_revision = 3;
         info->ram = "256M";
         info->manufacturer = "Sony UK";
         info->processor = "BCM2835";
      } else if (rev == 0x0013) {
         info->type = "Model B+";
         info->p1_revision = 3;
         info->ram = "512M";
         info->manufacturer = "Embest";
         info->processor = "BCM2835";
      } else if (rev == 0x0014) {
         info->type = "Compute Module 1";
         info->p1_revision = 0;
         info->ram = "512M";
         info->manufacturer = "Embest";
         info->processor = "BCM2835";
      } else if (rev == 0x0015) {
         info->type = "Model A+";
         info->p1_revision = 3;
         info->ram = "Unknown";
         info->manufacturer = "Embest";
         info->processor = "BCM2835";
      } else {  // don't know - assume revision 3 p1 connector
         info->p1_revision = 3;
      }
   }

   io_utils_fill_sys_info(info);
   return 0;
}

//=====================================================================
void io_utils_print_rpi_info(io_utils_sys_info_st *info)
{
    if (info == NULL)
    {
        ZF_LOGE("info is NULL");
        return;
    }

    printf("Raspberry Pi Information\n");
    printf("   Revision Code: %08X\n", info->p1_revision);
    printf("   RAM: %s\n", info->ram);
    printf("   Manu.: %s\n", info->manufacturer);
    printf("   Processor: %s\n", info->processor);
    printf("   Type: %s\n", info->type);
    printf("   Revision Text: %s\n", info->revision);
}