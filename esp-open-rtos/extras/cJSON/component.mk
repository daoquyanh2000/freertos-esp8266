# Component makefile for extras/cjson

# expected anyone using RTC driver includes it as 'cjson/cjson.h'
INC_DIRS += $(cJSON_ROOT)..

# args for passing into compile rule generation
cJSON_SRC_DIR =  $(cJSON_ROOT)

$(eval $(call component_compile_rules,cJSON))
