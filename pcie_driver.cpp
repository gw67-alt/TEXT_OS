#include "pcie_driver.h"      // Expected to contain DriverCommand, PCIeDevice struct definitions
#include "iostream_wrapper.h" // For cout, cin, std::hex, std::dec
#include "terminal_io.h"      // For terminal interaction
#include <stdint.h>           // For uintN_t types
#include <stddef.h>           // For size_t

// --- Configuration Constants ---
#define PCIE_CONFIG_ADDRESS 0xCF8
#define PCIE_CONFIG_DATA    0xCFC

#define PCIE_CONFIG_VENDOR_ID   0x00 // Word
#define PCIE_CONFIG_DEVICE_ID   0x02 // Word
#define PCIE_CONFIG_COMMAND     0x04 // Word
#define PCIE_CONFIG_STATUS      0x06 // Word
#define PCIE_CONFIG_CLASS_REV   0x08 // DWord: [ClassCode(byte)|Subclass(byte)|ProgIF(byte)|RevisionID(byte)]
#define PCIE_CONFIG_HEADER_TYPE 0x0E // Byte
#define PCIE_CONFIG_BAR0        0x10 // DWord
// ... other BARs ...

#define PCIE_CMD_IO_ENABLE     0x0001
#define PCIE_CMD_MEMORY_ENABLE 0x0002
#define PCIE_CMD_BUS_MASTER    0x0004

// --- Global Variables ---
static PCIeDevice detected_devices[32];
static int device_count = 0;
#define MAX_RETURNED_READ_VALUES 16 // Max read values to collect from one multi-command string

// --- Forward Declarations ---
bool parse_driver_command(const char* input, DriverCommand* cmd);
uint8_t driver_pcie_read(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
void driver_pcie_write(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value);
uint8_t driver_memory_read(uint32_t address);
void driver_memory_write(uint32_t address, uint8_t value);
void print_pcie_device_info(const PCIeDevice* dev);
void pcie_scan_devices();
static bool execute_parsed_driver_command(const DriverCommand* cmd, uint8_t* out_read_val, bool* was_read_op);

// --- Low-Level Port I/O Functions (only if not already defined) ---
#ifndef PORT_IO_DEFINED
static inline void outl(uint16_t port, uint32_t value) { asm volatile("outl %0, %1" : : "a"(value), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t value) { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint32_t inl(uint16_t port) { uint32_t v; asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline uint16_t inw(uint16_t port) { uint16_t v; asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
#endif

// --- String Utility Functions ---
static const char* my_strstr(const char* h, const char* n) { if(!h||!n)return 0;if(!*n)return h;for(;*h;h++){const char *ht=h,*nt=n;while(*ht&&*nt&&*ht==*nt){ht++;nt++;}if(!*nt)return h;}return 0;}
static int my_strncmp(const char* s1,const char* s2,size_t n){for(size_t i=0;i<n;i++){if(s1[i]!=s2[i])return (unsigned char)s1[i]-(unsigned char)s2[i];if(s1[i]=='\0')break;}return 0;}
static void safe_strncpy_line(char* d, const char* s, size_t n){if(n==0)return;size_t i=0;while(i<n-1&&s[i]!='\0'&&s[i]!='\n'&&s[i]!='\r'){d[i]=s[i];i++;}d[i]='\0';}

// --- Hex Conversion Utility Functions (implement if not in header) ---
uint8_t hex_char_to_value(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return 0xFF; // Return 0xFF for error
}

uint32_t hex_string_to_uint32(const char* hex_str) {
    uint32_t res=0; const char* p=hex_str; 
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    while(*p){uint8_t val=hex_char_to_value(*p++);if(val==0xFF)break;res=(res<<4)|val;} 
    return res;
}

uint8_t hex_string_to_uint8(const char* hex_str) {
    uint8_t res=0; const char* p=hex_str; 
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    for(int i=0;i<2&&*p;i++){uint8_t val=hex_char_to_value(*p++);if(val==0xFF)break;res=(res<<4)|val;} 
    return res;
}
// Fixed PCIe Configuration Space Access Functions
// The CF8/CFC method requires 32-bit aligned reads, then extract the needed bytes

uint32_t pcie_config_read32(uint8_t b, uint8_t d, uint8_t f, uint16_t o) {
    uint32_t a = (1U << 31) | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (o & 0xFCU);
    outl(PCIE_CONFIG_ADDRESS, a);
    return inl(PCIE_CONFIG_DATA);
}

uint16_t pcie_config_read16(uint8_t b, uint8_t d, uint8_t f, uint16_t o) {
    // Read the 32-bit value and extract the 16-bit portion
    uint32_t val32 = pcie_config_read32(b, d, f, o & 0xFC);
    uint8_t byte_offset = o & 0x03;
    return (val32 >> (byte_offset * 8)) & 0xFFFF;
}

uint8_t pcie_config_read8(uint8_t b, uint8_t d, uint8_t f, uint16_t o) {
    // Read the 32-bit value and extract the 8-bit portion
    uint32_t val32 = pcie_config_read32(b, d, f, o & 0xFC);
    uint8_t byte_offset = o & 0x03;
    return (val32 >> (byte_offset * 8)) & 0xFF;
}

void pcie_config_write32(uint8_t b, uint8_t d, uint8_t f, uint16_t o, uint32_t v) {
    uint32_t a = (1U << 31) | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (o & 0xFCU);
    outl(PCIE_CONFIG_ADDRESS, a);
    outl(PCIE_CONFIG_DATA, v);
}

void pcie_config_write16(uint8_t b, uint8_t d, uint8_t f, uint16_t o, uint16_t v) {
    // Read-modify-write for 16-bit values
    uint32_t val32 = pcie_config_read32(b, d, f, o & 0xFC);
    uint8_t byte_offset = o & 0x03;
    uint32_t mask = 0xFFFF << (byte_offset * 8);
    val32 = (val32 & ~mask) | ((uint32_t)v << (byte_offset * 8));
    pcie_config_write32(b, d, f, o & 0xFC, val32);
}

void pcie_config_write8(uint8_t b, uint8_t d, uint8_t f, uint16_t o, uint8_t v) {
    // Read-modify-write for 8-bit values
    uint32_t val32 = pcie_config_read32(b, d, f, o & 0xFC);
    uint8_t byte_offset = o & 0x03;
    uint32_t mask = 0xFF << (byte_offset * 8);
    val32 = (val32 & ~mask) | ((uint32_t)v << (byte_offset * 8));
    pcie_config_write32(b, d, f, o & 0xFC, val32);
}

// --- PCIe Device Utility Functions ---
bool pcie_device_exists(uint8_t b,uint8_t d,uint8_t f){
    uint16_t vid=pcie_config_read16(b,d,f,PCIE_CONFIG_VENDOR_ID);
    return(vid!=0xFFFF&&vid!=0x0000);
}

void print_pcie_device_info(const PCIeDevice* dev){
    if(!dev||!dev->valid)return;
    cout<<"  B"<<(int)dev->bus<<":D"<<(int)dev->device<<":F"<<(int)dev->function
        <<" VID:"<<std::hex<<dev->vendor_id<<" DID:"<<dev->device_id;
    
    // Use class_code instead of class_code_full
    uint8_t cc=(dev->class_code>>16)&0xFF;
    uint8_t sc=(dev->class_code>>8)&0xFF;
    uint8_t pi=(dev->class_code)&0xFF;
    
    cout<<" Cls:"<<(int)cc<<" Sub:"<<(int)sc<<" PI:"<<(int)pi<<std::dec<<"\n";
}

// --- PCIe Scan Function ---
void pcie_scan_devices(){
    device_count=0;
    for(uint16_t b=0;b<256&&device_count<32;b++){
        for(uint8_t d=0;d<32&&device_count<32;d++){
            if(!pcie_device_exists(b,d,0))continue;
            uint8_t ht=pcie_config_read8(b,d,0,PCIE_CONFIG_HEADER_TYPE);
            uint8_t max_f=(ht&0x80)?8:1;
            
            for(uint8_t f=0;f<max_f&&device_count<32;f++){
                if(pcie_device_exists(b,d,f)){
                    PCIeDevice* dv=&detected_devices[device_count];
                    dv->bus=b;dv->device=d;dv->function=f;
                    dv->vendor_id=pcie_config_read16(b,d,f,PCIE_CONFIG_VENDOR_ID);
                    dv->device_id=pcie_config_read16(b,d,f,PCIE_CONFIG_DEVICE_ID);
                    
                    // Read class code (use class_code member, not class_code_full)
                    uint32_t class_rev = pcie_config_read32(b,d,f,PCIE_CONFIG_CLASS_REV);
                    dv->class_code = (class_rev >> 8) & 0xFFFFFF; // Extract 24-bit class code
                    
                    // Store header type in a temporary variable since struct doesn't have it
                    uint8_t header_type = pcie_config_read8(b,d,f,PCIE_CONFIG_HEADER_TYPE);
                    
                    // Read BARs based on header type
                    int nbar=6;
                    if((header_type&0x7F)==1)nbar=2;
                    else if((header_type&0x7F)==2)nbar=1;
                    
                    for(int i=0;i<6;i++)dv->bar[i]=0;
                    for(int i=0;i<nbar;i++)
                        dv->bar[i]=pcie_config_read32(b,d,f,PCIE_CONFIG_BAR0+(i*4));
                    
                    dv->valid=true;
                    device_count++;
                }
                if(f==0&&!(ht&0x80))break;
            }
        }
    }
}

// --- Driver Core Read/Write Functions ---
uint8_t driver_memory_read(uint32_t a){return *((volatile uint8_t*)a);}
void driver_memory_write(uint32_t a,uint8_t v){*((volatile uint8_t*)a)=v;}

uint8_t driver_pcie_read(uint8_t b,uint8_t d,uint8_t f,uint16_t o){
    if(pcie_device_exists(b,d,f))return pcie_config_read8(b,d,f,o);
    cout<<"PCIe Read Err: Dev "<<(int)b<<":"<<(int)d<<":"<<(int)f<<" not found!\n";
    return 0xFF;
}

void driver_pcie_write(uint8_t b,uint8_t d,uint8_t f,uint16_t o,uint8_t v){
    if(pcie_device_exists(b,d,f))pcie_config_write8(b,d,f,o,v);
    else cout<<"PCIe Write Err: Dev "<<(int)b<<":"<<(int)d<<":"<<(int)f<<" not found!\n";
}

// --- Command Execution Helper ---
static bool execute_parsed_driver_command(const DriverCommand* cmd, uint8_t* out_read_val, bool* was_read_op) {
    if (!cmd || !out_read_val || !was_read_op) return false;
    *was_read_op = false; *out_read_val = 0;

    if (cmd->is_read) {
        *was_read_op = true;
        if (cmd->use_pcie) {
            cout << "  Exec: Read PCIe " << (int)cmd->bus << ":" << (int)cmd->device << ":" << (int)cmd->function << "@" << std::hex << cmd->offset << std::dec;
            *out_read_val = driver_pcie_read(cmd->bus, cmd->device, cmd->function, cmd->offset);
            cout << " -> Val:" << std::hex << (int)(*out_read_val) << std::dec << "\n";
        } else {
            cout << "  Exec: Read Mem @" << std::hex << cmd->address << std::dec;
            *out_read_val = driver_memory_read(cmd->address);
            cout << " -> Val:" << std::hex << (int)(*out_read_val) << std::dec << "\n";
        }
        return true; // Read attempted
    } else { // Write
        if (cmd->use_pcie) {
            cout << "  Exec: Write " << std::hex << (int)cmd->value << std::dec << " to PCIe " << (int)cmd->bus << ":" << (int)cmd->device << ":" << (int)cmd->function << "@" << std::hex << cmd->offset << std::dec << "\n";
            driver_pcie_write(cmd->bus, cmd->device, cmd->function, cmd->offset, cmd->value);
        } else {
            cout << "  Exec: Write " << std::hex << (int)cmd->value << std::dec << " to Mem @" << std::hex << cmd->address << std::dec << "\n";
            driver_memory_write(cmd->address, cmd->value);
        }
        return true; // Write attempted
    }
    return false; // Should not be reached
}

// --- Command Parsing Functions ---
static bool parse_pcie_specification(const char* pcie_start_param, DriverCommand* cmd) {
    const char* pcie_start = pcie_start_param;
    while (*pcie_start == ' ') pcie_start++;
    if (my_strncmp(pcie_start, "pcie:", 5) == 0) {
        cmd->use_pcie = true; const char* p = pcie_start + 5; uint32_t parts[4]; int part_idx = 0;
        const char* seg_start = p;
        for (;; p++) {
            if (*p == ':' || *p == '\0' || *p == ' ') {
                if (p > seg_start) { 
                    char temp_buf[12]; size_t len = p - seg_start; 
                    if(len >= sizeof(temp_buf)) return false; 
                    for(size_t k=0;k<len;k++)temp_buf[k]=seg_start[k];
                    temp_buf[len]='\0'; 
                    parts[part_idx++] = hex_string_to_uint32(temp_buf); 
                }
                if (*p == '\0' || *p == ' ' || part_idx == 4) break;
                seg_start = p + 1; 
                if (part_idx == 4 && *p == ':') return false; // Too many colons
            } else if (!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')||(*p=='x'||*p=='X'))) return false; // Invalid char
        }
        if (part_idx == 4) { 
            cmd->bus=(uint8_t)parts[0]; 
            cmd->device=(uint8_t)parts[1]; 
            cmd->function=(uint8_t)parts[2]; 
            cmd->offset=(uint16_t)parts[3]; 
            return true; 
        }
    }
    return false;
}

bool parse_driver_command(const char* input_param, DriverCommand* cmd) {
    const char* input = input_param;
    cmd->use_pcie=false; cmd->is_read=false; cmd->value=0; cmd->address=0; cmd->bus=0; cmd->device=0; cmd->function=0; cmd->offset=0;

    const char* cur = my_strstr(input, "driver"); if (!cur) return false; cur += 6;
    cur = my_strstr(cur, ">>"); if (!cur) return false; cur += 2; while (*cur == ' ') cur++;

    char p1[32]; const char* p1e = cur; while(*p1e && *p1e != ' ') p1e++; size_t p1l = p1e - cur;
    if (p1l==0||p1l>=sizeof(p1)) return false; for(size_t i=0;i<p1l;i++)p1[i]=cur[i]; p1[p1l]='\0';

    if (my_strncmp(p1, "read", 4) == 0 && p1[4]=='\0') cmd->is_read = true;
    else cmd->value = hex_string_to_uint8(p1);
    cur = p1e; while (*cur == ' ') cur++;

    if (my_strncmp(cur, ">>", 2)!=0) return false; cur += 2; while (*cur == ' ') cur++;

    char p3[32]; const char* p3e = cur; while(*p3e && *p3e != ' ') p3e++; size_t p3l = p3e - cur;
    if (p3l==0||p3l>=sizeof(p3)) return false; for(size_t i=0;i<p3l;i++)p3[i]=cur[i]; p3[p3l]='\0';
    cmd->address = hex_string_to_uint32(p3); // This address might be ignored for PCIe if BDFO is present
    cur = p3e; while (*cur == ' ') cur++;

    if (my_strncmp(cur, ">>", 2) == 0) {
        cur += 2; while (*cur == ' ') cur++;
        if (!parse_pcie_specification(cur, cmd)) return false;
    } else if (*cur != '\0') { /* Trailing chars? */ }
    return true;
}

// --- Multi-Command String Processor ---
static void process_multi_command_string(const char* multi_cmd_string,
                                  uint8_t* read_values_buffer, 
                                  int buffer_capacity,          
                                  int* actual_reads_count) {    
    const char* current_cmd_start = multi_cmd_string;
    char single_cmd_buf[256];
    if (actual_reads_count) *actual_reads_count = 0;

    if (!multi_cmd_string || !read_values_buffer || !actual_reads_count) return;

    while (*current_cmd_start) {
        while (*current_cmd_start == ' ' || *current_cmd_start == '\t' || *current_cmd_start == ';') {
            if (*current_cmd_start == '\0') break; current_cmd_start++;
        }
        if (!*current_cmd_start) break;

        const char* next_separator = current_cmd_start;
        while (*next_separator != '\0' && *next_separator != ';') next_separator++;
        size_t cmd_len = next_separator - current_cmd_start;
        while (cmd_len > 0 && (current_cmd_start[cmd_len-1]==' '||current_cmd_start[cmd_len-1]=='\t')) cmd_len--;

        if (cmd_len > 0 && cmd_len < sizeof(single_cmd_buf)) {
            for(size_t i=0;i<cmd_len;i++) single_cmd_buf[i]=current_cmd_start[i]; 
            single_cmd_buf[cmd_len]='\0';

            if (single_cmd_buf[0] != '\0') {
                const char* list_check_ptr=single_cmd_buf; bool is_list=false;
                if(my_strstr(list_check_ptr,"driver")==list_check_ptr){
                    list_check_ptr=my_strstr(list_check_ptr,">>");
                    if(list_check_ptr){
                        list_check_ptr+=2;while(*list_check_ptr==' ')list_check_ptr++;
                        if(my_strncmp(list_check_ptr,"list",4)==0&&(list_check_ptr[4]=='\0'||list_check_ptr[4]==' '))
                            is_list=true;
                    }
                }
                
                if(is_list){
                    cout<<"Detected PCIe devices:\n";
                    for(int i=0;i<device_count;i++)print_pcie_device_info(&detected_devices[i]);
                } else {
                    DriverCommand cmd;
                    if (parse_driver_command(single_cmd_buf, &cmd)) {
                        uint8_t temp_val; bool was_read;
                        execute_parsed_driver_command(&cmd, &temp_val, &was_read);
                        if (was_read && *actual_reads_count < buffer_capacity) {
                            read_values_buffer[*actual_reads_count] = temp_val;
                            (*actual_reads_count)++;
                        }
                    } else { 
                        cout << "Error: Failed to parse segment: \"" << single_cmd_buf << "\"\n"; 
                    }
                }
            }
        }
        if (*next_separator == ';') current_cmd_start = next_separator + 1;
        else break;
    }
}

// --- Script Execution Function ---
void execute_driver_script_from_buffer(const char* script_buffer) {
    if (!script_buffer) { cout << "Script Error: null buffer.\n"; return; }
    const char* line_start = script_buffer;
    char line_buf[256];
    uint8_t line_read_values[MAX_RETURNED_READ_VALUES]; 
    int line_num_reads;

    cout << "Executing script...\n";
    while (*line_start) {
        const char* line_end = line_start;
        while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') line_end++;
        size_t line_len = line_end - line_start;

        if (line_len < sizeof(line_buf)) {
            safe_strncpy_line(line_buf, line_start, line_len + 1);
            char* trimmed = line_buf; 
            while(*trimmed==' '||*trimmed=='\t')trimmed++;
            if (*trimmed != '\0' && *trimmed != '#') {
                process_multi_command_string(trimmed, line_read_values, MAX_RETURNED_READ_VALUES, &line_num_reads);
                if (line_num_reads > 0) {
                    cout << "  Values read from this line: ";
                    for (int i = 0; i < line_num_reads; i++) {
                        cout << "" << std::hex << (int)line_read_values[i] << std::dec << (i == line_num_reads - 1 ? "" : ", ");
                    }
                    cout << "\n";
                }
            }
        } else if (line_len > 0) { 
            cout << "Script Warning: Line too long, skipping.\n"; 
        }
        if (*line_end == '\0') break;
        line_start = line_end + 1;
    }
    cout << "Script execution finished.\n";
}

// --- Main Command Handlers ---
void driver_cfg(char* input_command_string, 
                bool* overall_parsing_success, 
                uint8_t* out_read_values_array, 
                int buffer_capacity, 
                int* num_values_read_actual) {
    if(overall_parsing_success) *overall_parsing_success = true; 
    if(num_values_read_actual) *num_values_read_actual = 0;

    process_multi_command_string(input_command_string, 
                                 out_read_values_array, 
                                 buffer_capacity, 
                                 num_values_read_actual);
}


// --- Initialization Function ---
void init_pcie_driver() {
    cout << "Initializing PCIe Driver...\n";
    device_count = 0;
    for (int i = 0; i < 32; i++) detected_devices[i].valid = false;
    pcie_scan_devices();
    cout << "PCIe Driver initialized. Found " << device_count << " PCIe function(s).\n";
}
