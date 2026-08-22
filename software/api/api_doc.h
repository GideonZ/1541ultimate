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
 *   DEPRECATED(reason)                           optional, marks the operation
 *   CAUTION("hint,hint", note)                   optional, says what the call
 *                                                does beyond answering
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
 * belongs to, or "" for all of them. A RESPONSE scoped to an operation replaces
 * the unscoped one for the same status code, whichever order they are written in.
 *
 * A default or an example has to be a literal of the type the directive declares:
 * "true" or "false" for a boolean, and a number within the range for a ranged
 * integer. Anything else fails the build rather than becoming a plausible value.
 *
 * Nothing may be declared twice: one PARAM, PATH_PARAM, PARAM_ENUM or
 * PATH_PARAM_ENUM per name, one RESPONSE per status code per scope, one
 * RESPONSE_EXAMPLE per name per code, one BODY per content type, and one
 * PATH per template and verb. A second declaration is refused rather than
 * replacing the first, because only one of them could reach the document.
 *
 * CAUTION hints come from a closed set, also in tools/openapi/schemas.py, so
 * that a caller can act on them without reading English: destructive,
 * machine-state, persistent, power, diagnostic, idempotent. They reach the
 * document as an `x-ultimate-caution` field on the operation.
 */

#define API_DOC(...)

#endif // API_DOC_H
