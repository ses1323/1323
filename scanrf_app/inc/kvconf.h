#ifndef CLL_KVCONF_H
#define CLL_KVCONF_H

#define KEYVALLEN 256

/*   删除左边的空格   */
char * l_trim(char * sz_output, const char *sz_input);

/*   删除右边的空格   */
char *r_trim(char *sz_output, const char *sz_input);

/*   删除两边的空格   */
char * a_trim(char * sz_output, const char * sz_input);


int get_profile_string(char *profile, char *key_name, char *key_val );


#endif //CLL_KVCONF_H

