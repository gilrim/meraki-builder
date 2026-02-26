#ifndef configd
#define configd

struct json_object *get_status(void);
struct json_object *read_config();
int write_config(struct json_object *json);

#endif
