/*
    Combined compilation of server
 */

#include "esp.h"

/*
    Source from /Users/mob/dev/appweb-core/src/server/web/test/test.esp
 */
/*
   Generated from web/test/test.esp
 */
#include "esp.h"

static void view_cc3d9507a3a077023b72faa77f425cd7(HttpStream *stream) {
  espRenderBlock(stream, "<html>\n\
<body>\n\
	<p>", 18);
render("Hello ESP World");   espRenderBlock(stream, "</p>\n\
</body>\n\
</html>\n\
", 21);
}

ESP_EXPORT int esp_view_cc3d9507a3a077023b72faa77f425cd7(HttpRoute *route) {
   espDefineView(route, "test/test.esp", view_cc3d9507a3a077023b72faa77f425cd7);
   return 0;
}



ESP_EXPORT int esp_app_server_combine(HttpRoute *route) {
    esp_view_cc3d9507a3a077023b72faa77f425cd7(route);
    return 0;
}
