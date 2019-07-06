#ifndef _JSON_WRAPPER_H_
#define _JSON_WRAPPER_H_

#include "json.h"

#define json_get_type(val)              ((val)->type)
/* type == json_object */
#define json_get_object_length(val)     ((val)->u.object.length)
#define json_get_object_name(val, i)    ((val)->u.object.values[i].name)
#define json_get_object_value(val, i)   ((val)->u.object.values[i].value)
/* type == json_array */
#define json_get_array_length(val)      ((val)->u.array.length)
#define json_get_array_value(val, i)    ((val)->u.array.values[i])
/* type == json_boolean */
#define json_get_bool(val)              ((val)->u.boolean)
/* type == json_integer */
#define json_get_int(val)               ((val)->u.integer)
/* type == json_double */
#define json_get_double(val)            ((val)->u.dbl)
/* type == json_string */
#define json_get_string(val)            ((val)->u.string.ptr)
#define json_get_string_length(val)     ((val)->u.string.length)

#endif // _JSON_WRAPPER_H_