// Retail uses newlib malloc (also through heap_3 and operator new), whereas
// newer upstream uses a FreeRTOS heap. Do not label arena slack as total free
// RAM or invent a low-water mark. ESP32 allocations are a separate allocator.
#include <malloc.h>
#include "https_management.h"

API_CALL(GET, machine, heap, NULL, ARRAY( {  }))
{
    const struct mallinfo memory = mallinfo();
    resp->json->add("allocator", "newlib");
    resp->json->add("allocated", (int)memory.uordblks);
    resp->json->add("arena", (int)memory.arena);
    resp->json->add("free_in_arena", (int)memory.fordblks);
    resp->json->add("free_blocks", (int)memory.ordblks);
    https_management_add_metrics(resp->json);
    resp->json_response(HTTP_OK);
}
