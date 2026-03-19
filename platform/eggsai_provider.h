#ifndef EGGSAI_PROVIDER_H
#define EGGSAI_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "../cJSON/cJSON.h"
#include "../lvgl/lvgl.h" /* for lv_async_call */
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/**********************
 *      MACROS
 **********************/
#define EGGSAI_MAX_MODELS       32
#define EGGSAI_MAX_NAME_LEN     64
#define EGGSAI_RESPONSE_MAX_LEN 8192
#define EGGSAI_CONFIG_DIR       "/mnt/UDISK/eggai/config/"

/**********************
 *      TYPEDEFS
 **********************/

/**
 * AI response callback type.
 * @param response  Null-terminated string containing the AI's reply.
 *                  If success is false, this contains an error message.
 * @param success   True if the request completed and returned valid text.
 * @param user_data Opaque pointer passed during send_message.
 */
typedef void (*eggsai_response_cb_t)(const char * response, bool success, void * user_data);

/**
 * Represents a single configured AI Model (loaded from a JSON file).
 */
typedef struct {
    char name[EGGSAI_MAX_NAME_LEN];   /* Display name for UI dropdown */
    
    char * endpoint;                  /* API URL */
    char * method;                    /* "POST" or "GET" */
    
    cJSON * headers;                  /* JSON object of HTTP headers */
    cJSON * body_template;            /* JSON object of HTTP body with "{{PROMPT}}" inside */
    cJSON * response_extractor;       /* JSON array of strings/ints for path parsing */
    
} EggsaiModel;

/**
 * The generic API provider interface.
 */
typedef struct _EggsaiProvider {
    char name[EGGSAI_MAX_NAME_LEN];

    /* Array of loaded API configurations */
    EggsaiModel models[EGGSAI_MAX_MODELS];
    int         model_count;

    /**
     * Scan the config directory and load all valid .json files.
     * @param provider Pointer to the provider instance.
     * @return 0 on success, non-zero on error.
     */
    int (*load_configs)(struct _EggsaiProvider * provider);

    /**
     * Send a single-turn message to the specified model asynchronously.
     * @param provider   Pointer to the provider instance.
     * @param model_idx  Index of the model in the models array.
     * @param message    The user's prompt (null-terminated).
     * @param cb         Callback fired in the LVGL thread when response arrives.
     * @param user_data  Opaque pointer passed to the callback.
     * @return 0 if request started successfully, non-zero if immediate error.
     */
    int (*send_message)(struct _EggsaiProvider * provider, int model_idx, const char * message, 
                        eggsai_response_cb_t cb, void * user_data);
} EggsaiProvider;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Get the singleton instance of the generic EggsaiProvider.
 */
EggsaiProvider * eggsai_provider_get_default(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* EGGSAI_PROVIDER_H */
