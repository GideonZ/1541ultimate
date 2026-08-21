#ifndef API_DOC_H
#define API_DOC_H

/*
 * Documents the REST call that the API_CALL below it registers.
 *
 * API_DOC expands to nothing, and because its parameters do not appear in the
 * replacement list the preprocessor never expands what is inside it either, so
 * the directives need no definitions and none of this text reaches the image.
 * tools/openapi/generate.py reads the blocks out of the sources and writes
 * doc/api/rest_api_openapi_u2.yaml and doc/api/rest_api_openapi_u64.yaml. It
 * refuses to emit anything when a block and its API_CALL disagree, so run
 * `make openapi` after changing either.
 *
 *   API_DOC(GET, machine, readmem,
 *       TAG("Machine")
 *       SUMMARY("Read C64 memory")
 *       DESCRIPTION("Performs a DMA read on the cartridge bus and returns the "
 *                   "bytes as a binary attachment.")
 *       PATH("/v1/machine:readmem", "readMemory", "")
 *       PARAM("address", "string", "Start address in hexadecimal.", "", "D020")
 *       PARAM("length", "integer(1..65536)", "Bytes to read.", "256", "2")
 *       RESPONSE("200", "application/octet-stream", "", "The bytes read.", "")
 *       RESPONSE_ERROR("400", "Invalid address", "")
 *   )
 *   API_CALL(GET, machine, readmem, NULL, ARRAY( { {"address", P_REQUIRED},
 *                                                  {"length", P_OPTIONAL} }))
 *
 * Every argument is a string literal; adjacent literals are concatenated.
 *
 *   TAG(name)                                    exactly one
 *   SUMMARY(text)                                exactly one
 *   DESCRIPTION(text)                            exactly one
 *   PATH(template, operation_id, summary)        one or more; summary "" keeps
 *                                                the SUMMARY above
 *   PATH_PARAM(name, type, description, example) one per {placeholder}
 *   PATH_PARAM_ENUM(name, "a,b,c")               optional
 *   PARAM(name, type, description, default, ex)  one per API_CALL parameter
 *   PARAM_ENUM(name, "a,b,c")                    optional
 *   BODY(content_type, schema, description)      exactly when API_CALL has a
 *                                                body handler; schema "" is an
 *                                                opaque payload
 *   RESPONSE(code, type, schema, descr, scope)   one or more
 *   RESPONSE_EXAMPLE(code, name, json, scope)    optional
 *   RESPONSE_ERROR(code, message, scope)         becomes an errors[] example
 *
 * A type is "string", "integer", "boolean", or "integer(low..high)". A schema is
 * a name from tools/openapi/schemas.py. A scope is the operation_id the response
 * belongs to, or "" for all of them.
 */

#define API_DOC(...)

#endif // API_DOC_H
