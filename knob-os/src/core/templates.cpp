#include "templates.h"

#include <cstring>

TemplateScreen* make_dial(JsonObjectConst data, TemplateSink sink);
TemplateScreen* make_tiles(JsonObjectConst data, TemplateSink sink);
TemplateScreen* make_list(JsonObjectConst data, TemplateSink sink);
TemplateScreen* make_media(JsonObjectConst data, TemplateSink sink);
TemplateScreen* make_controls(JsonObjectConst data, TemplateSink sink);
TemplateScreen* make_ring(JsonObjectConst data, TemplateSink sink);

TemplateScreen* make_template(const char* name, JsonObjectConst data, TemplateSink sink) {
    if (!name) return nullptr;
    if (std::strcmp(name, "dial") == 0) return make_dial(data, std::move(sink));
    if (std::strcmp(name, "tiles") == 0) return make_tiles(data, std::move(sink));
    if (std::strcmp(name, "list") == 0) return make_list(data, std::move(sink));
    if (std::strcmp(name, "media") == 0) return make_media(data, std::move(sink));
    if (std::strcmp(name, "controls") == 0) return make_controls(data, std::move(sink));
    if (std::strcmp(name, "ring") == 0) return make_ring(data, std::move(sink));
    return nullptr;
}
