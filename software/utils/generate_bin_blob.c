/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <time.h>


#define LINE_LEN  16

//-------------------------------------------------------------------
int file_exists(char* fname, int *size, int *dir, int *file, int *dev)
{
	struct stat st;
	if(stat(fname,&st) != 0)
	{
    	return 0;
	}

	if (dir) *dir = S_ISDIR(st.st_mode);
	if (file) *file = S_ISREG(st.st_mode);
	if (dev) *dev =  S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode);
	if (size) *size = st.st_size;

	return 1;
}

//-------------------------------------------------------------------
void get_filename_ext_from_path(char* full_path, char* file_path, char* file_name, char* file_ext)
{
    char path[256] = {0};
    strcpy(path, full_path);

    char* ext = NULL;
    char* name = NULL;
    
    for(size_t i = strlen(path) - 1; i; i--)
    {
        if (path[i] == '.' && ext == NULL)
        {
            ext = &path[i+1];
            path[i] = '\0';
        }

        if (path[i] == '/' && name == NULL)
        {
            name = &path[i+1];
            path[i] = '\0';
        }
    }
    if (name == NULL) 
    {
        name = &path[0];
        if (file_path) strcpy(file_path, "");
    }
    else
    {
        if (file_path) strcpy(file_path, path);
    }
    if (file_name) strcpy(file_name, name);
    if (file_ext) strcpy(file_ext, ext);
}

//-------------------------------------------------------------------

int main(int argc, char *argv[])
{
    char path[128], name[128], ext[128];
    int size_of_file = 0;

    if (argc < 4)
    {
        printf("Code (C/C++ Header) generation from binary file, Ver 1.0\n");
        printf("Usage: generate_code [binary_file] [variable_name] [code filename]\n");
        printf("    binary_file - the file to be converted into a code blob\n");
        printf("    variable name - The variable name of type uint8_t[] within the code file\n");
        printf("    code filename - the filename of the generated c/c++ code (typically a header file)\n");
        return 0;
    }

    char *bin_file = argv[1];
    char *var_name = argv[2];
    char *code_file = argv[3];

    if ( !file_exists(bin_file, &size_of_file, NULL, NULL, NULL) )
    {
        printf("binary file doesn't exist: %s\n", bin_file);
        return -1;
    }

    // open the binary file
    FILE* f_bin = NULL;
    f_bin = fopen(bin_file, "rb");
    if (f_bin == NULL)
    {
        printf("binary file doesn't exist: %s\n", bin_file);
        return -1;
    }

    FILE* f_code = fopen (code_file, "w");
    if (f_code == NULL)
    {
        printf("Couldn't open the code file %s for writing\n", code_file);
        fclose(f_bin);
        return -1;
    }

    get_filename_ext_from_path(code_file, path, name, ext);
    printf("The code filename is path: '%s', name: '%s', ext: '%s'\n", 
            path, name, ext);

    fprintf(f_code, "/*\n"
                    " * This file was automatically generated using the 'generate_bin_blob' tool\n"
                    " * Modification of this file is not recommanded - please re-generate it\n"
                    " * as needed and embed in the code library.\n"
                    " */\n\n");

    fprintf(f_code, "#ifndef __%s_%s__\n", name, ext);
    fprintf(f_code, "#define __%s_%s__\n\n", name, ext);
    
    fprintf(f_code, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    fprintf(f_code, "#include <stdio.h>\n");
    fprintf(f_code, "#include <stdint.h>\n");
    fprintf(f_code, "#include <time.h>\n\n");


    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    fprintf(f_code, "/*\n"
                    " * Time tagging of the module through the 'struct tm' structure \n"
                    " *     Date: %d-%02d-%02d\n"
                    " *     Time: %02d:%02d:%02d\n"
                    " */\n", 
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    fprintf(f_code, "struct tm %s_date_time = {\n", var_name);
    fprintf(f_code, "   .tm_sec = %d,\n", tm.tm_sec);
    fprintf(f_code, "   .tm_min = %d,\n", tm.tm_min);
    fprintf(f_code, "   .tm_hour = %d,\n", tm.tm_hour);
    fprintf(f_code, "   .tm_mday = %d,\n", tm.tm_mday);
    fprintf(f_code, "   .tm_mon = %d,   /* +1    */\n", tm.tm_mon);
    fprintf(f_code, "   .tm_year = %d,  /* +1900 */\n", tm.tm_year);
    fprintf(f_code, "};\n\n");
    
    fprintf(f_code, "/*\n"
                    " * Data blob of variable %s:\n"
                    " *     Size: %d bytes\n"
                    " *     Original filename: %s\n"
                    " */\n", var_name, size_of_file, bin_file);
    fprintf(f_code, "uint8_t %s[] = {", var_name);

    for (int j = 0; j < size_of_file; j ++)
    {
        uint8_t b = 0;
        fread(&b, 1, 1, f_bin);

        if ((j % LINE_LEN) == 0) fprintf(f_code, "\n\t");
        fprintf(f_code, "0x%02X, ", b);
        if (j == (size_of_file - 1)) fprintf(f_code, "\n};\n\n");
    }

    fprintf(f_code, "#ifdef __cplusplus\n}\n#endif\n\n");
    fprintf(f_code, "#endif // __%s_%s__\n", name, ext);

    fclose(f_bin);
    fclose(f_code);

    return 0;
}
