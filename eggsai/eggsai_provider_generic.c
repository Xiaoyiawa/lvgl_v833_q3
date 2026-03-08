#include "eggsai_provider.h"
#include "../cJSON/cJSON.h"
#include "../lvgl/lvgl.h" /* for lv_async_call */
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* Forward declare the generic provider singleton */
static EggsaiProvider s_generic_provider;
static bool s_provider_initialized = false;

/* Data structure passed to the curl worker thread */
typedef struct {
    int         model_idx;
    char *      user_message;
    eggsai_response_cb_t cb;
    void *      user_data;
} WorkerData;

/* Data structure passed from thread back to LVGL main thread via lv_async_call */
typedef struct {
    char *      response_text;
    bool        success;
    eggsai_response_cb_t cb;
    void *      user_data;
} AsyncResponseData;

/* curl write callback */
struct memory_struct {
    char * memory;
    size_t size;
};

static size_t write_cb(void * contents, size_t size, size_t nmemb, void * userp)
{
    size_t realsize = size * nmemb;
    struct memory_struct * mem = (struct memory_struct *)userp;

    char * ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        return 0;  /* out of memory! */
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/* Invoked safely inside LVGL UI thread */
static void delivery_cb(void * param)
{
    AsyncResponseData * async_data = (AsyncResponseData *)param;
    if(async_data->cb) {
        async_data->cb(async_data->response_text, async_data->success, async_data->user_data);
    }
    if(async_data->response_text) {
        free(async_data->response_text);
    }
    free(async_data);
}

/* Recursive function to find "{{PROMPT}}" in a cJSON object/array and replace it */
static void replace_prompt_in_cjson(cJSON * item, const char * prompt)
{
    if(!item) return;

    if(cJSON_IsString(item) && item->valuestring) {
        if(strstr(item->valuestring, "{{PROMPT}}") != NULL) {
            /* Replace the entire string with the prompt for now 
               (advanced templating could do sub-string replace, but APIs usually just need the whole string) */
            /* We will just check if "{{PROMPT}}" is present, and set the whole string to the user prompt */
            cJSON_SetValuestring(item, prompt);
        }
    } else if(cJSON_IsArray(item) || cJSON_IsObject(item)) {
        cJSON * child = NULL;
        cJSON_ArrayForEach(child, item) {
            replace_prompt_in_cjson(child, prompt);
        }
    }
}

/* Follows the JSON path in the extractor array to get the final string */
static const char * extract_response(cJSON * root, cJSON * extractor)
{
    if(!root || !extractor || !cJSON_IsArray(extractor)) return NULL;

    cJSON * current = root;
    cJSON * step = NULL;

    cJSON_ArrayForEach(step, extractor) {
        if(!current) return NULL;

        if(cJSON_IsString(step)) {
            if(!cJSON_IsObject(current)) return NULL;
            current = cJSON_GetObjectItemCaseSensitive(current, step->valuestring);
        } else if(cJSON_IsNumber(step)) {
            if(!cJSON_IsArray(current)) return NULL;
            current = cJSON_GetArrayItem(current, step->valueint);
        } else {
            return NULL;
        }
    }

    if(current && cJSON_IsString(current)) {
        return current->valuestring;
    }
    return NULL;
}

/* Background thread doing the network I/O */
static void * curl_worker_thread(void * arg)
{
    WorkerData * task = (WorkerData *)arg;
    AsyncResponseData * result = calloc(1, sizeof(AsyncResponseData));
    result->cb = task->cb;
    result->user_data = task->user_data;
    result->success = false;

    if(task->model_idx < 0 || task->model_idx >= s_generic_provider.model_count) {
        result->response_text = strdup("Invalid model index");
        goto end;
    }

    EggsaiModel * model = &s_generic_provider.models[task->model_idx];

    CURL * curl = curl_easy_init();
    if(!curl) {
        result->response_text = strdup("Failed to init curl");
        goto end;
    }

    struct memory_struct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    /* Build HTTP headers */
    struct curl_slist * headers_slist = NULL;
    if(model->headers && cJSON_IsObject(model->headers)) {
        cJSON * hdr = NULL;
        cJSON_ArrayForEach(hdr, model->headers) {
            if(cJSON_IsString(hdr)) {
                char header_line[512];
                snprintf(header_line, sizeof(header_line), "%s: %s", hdr->string, hdr->valuestring);
                headers_slist = curl_slist_append(headers_slist, header_line);
            }
        }
    }

    /* Build HTTP body */
    char * body_str = NULL;
    if(model->body_template) {
        /* Deep copy the template so we can mutate it */
        cJSON * request_json = cJSON_Duplicate(model->body_template, 1);
        if(request_json) {
            replace_prompt_in_cjson(request_json, task->user_message);
            body_str = cJSON_PrintUnformatted(request_json);
            cJSON_Delete(request_json);
        }
    }

    /* Setup curl */
    curl_easy_setopt(curl, CURLOPT_URL, model->endpoint);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_slist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    /* Disable SSL Verification for generic device compatibility */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if(model->method && strcmp(model->method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if(body_str) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
        }
    }

    CURLcode res = curl_easy_perform(curl);
    
    if(res != CURLE_OK) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Curl error: %s", curl_easy_strerror(res));
        result->response_text = strdup(err_buf);
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        
        if(http_code == 200) {
            cJSON * root = cJSON_Parse(chunk.memory);
            if(root) {
                const char * reply_text = extract_response(root, model->response_extractor);
                if(reply_text) {
                    result->response_text = strdup(reply_text);
                    result->success = true;
                } else {
                    result->response_text = strdup("Error: Failed to extract response from JSON geometry.");
                }
                cJSON_Delete(root);
            } else {
                result->response_text = strdup("Error: Invalid JSON response");
            }
        } else {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "HTTP Error %ld", http_code);
            result->response_text = strdup(err_buf);
        }
    }

    /* Cleanup */
    if(headers_slist) curl_slist_free_all(headers_slist);
    if(body_str) free(body_str);
    free(chunk.memory);
    curl_easy_cleanup(curl);

end:
    free(task->user_message);
    free(task);
    
    /* Schedule UI update on main thread */
    lv_async_call(delivery_cb, result);
    return NULL;
}

static int generic_send_message(EggsaiProvider * provider, int model_idx, const char * message, 
                                eggsai_response_cb_t cb, void * user_data)
{
    (void)provider;
    if(model_idx < 0 || model_idx >= provider->model_count) return -1;
    if(!message || strlen(message) == 0) return -1;

    WorkerData * data = calloc(1, sizeof(WorkerData));
    if(!data) return -1;

    data->model_idx = model_idx;
    data->user_message = strdup(message);
    data->cb = cb;
    data->user_data = user_data;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if(pthread_create(&thread, &attr, curl_worker_thread, data) != 0) {
        free(data->user_message);
        free(data);
        pthread_attr_destroy(&attr);
        return -1;
    }

    pthread_attr_destroy(&attr);
    return 0;
}

static int generic_load_configs(EggsaiProvider * provider)
{
    provider->model_count = 0;
    
    DIR * dir = opendir(EGGSAI_CONFIG_DIR);
    if(!dir) {
        printf("[eggsai] Warning: Could not open config dir %s\n", EGGSAI_CONFIG_DIR);
        return -1; /* Directory probably doesn't exist yet */
    }

    struct dirent * ent;
    while((ent = readdir(dir)) != NULL) {
        if(provider->model_count >= EGGSAI_MAX_MODELS) break;

        /* Check for .json extension */
        const char * ext = strrchr(ent->d_name, '.');
        if(!ext || strcmp(ext, ".json") != 0) continue;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s%s", EGGSAI_CONFIG_DIR, ent->d_name);

        FILE * f = fopen(filepath, "rb");
        if(!f) continue;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char * json_str = malloc(fsize + 1);
        if(!json_str) {
            fclose(f);
            continue;
        }
        size_t read_bytes = fread(json_str, 1, fsize, f);
        json_str[read_bytes] = 0;
        fclose(f);

        cJSON * root = cJSON_Parse(json_str);
        free(json_str);

        if(!root) {
            printf("[eggsai] Error parsing JSON config: %s\n", filepath);
            continue;
        }

        cJSON * j_name = cJSON_GetObjectItemCaseSensitive(root, "name");
        cJSON * j_endpoint = cJSON_GetObjectItemCaseSensitive(root, "endpoint");
        cJSON * j_method = cJSON_GetObjectItemCaseSensitive(root, "method");
        cJSON * j_headers = cJSON_GetObjectItemCaseSensitive(root, "headers");
        cJSON * j_body = cJSON_GetObjectItemCaseSensitive(root, "body_template");
        cJSON * j_extractor = cJSON_GetObjectItemCaseSensitive(root, "response_extractor");

        if(j_name && j_endpoint && j_method && j_body && j_extractor && cJSON_IsString(j_name)) {
            EggsaiModel * model = &provider->models[provider->model_count];
            strncpy(model->name, j_name->valuestring, sizeof(model->name) - 1);
            model->name[sizeof(model->name) - 1] = '\0';
            
            model->endpoint = strdup(j_endpoint->valuestring);
            model->method = strdup(j_method->valuestring);
            
            /* Detach these cJSON items from the root so we can keep them after deleting root */
            if(j_headers) model->headers = cJSON_DetachItemViaPointer(root, j_headers);
            model->body_template = cJSON_DetachItemViaPointer(root, j_body);
            model->response_extractor = cJSON_DetachItemViaPointer(root, j_extractor);

            provider->model_count++;
            printf("[eggsai] Loaded config: %s (%s)\n", model->name, ent->d_name);
        } else {
            printf("[eggsai] Invalid config format in %s\n", filepath);
        }

        cJSON_Delete(root);
    }
    
    closedir(dir);
    return 0;
}

EggsaiProvider * eggsai_provider_get_default(void)
{
    if(!s_provider_initialized) {
        memset(&s_generic_provider, 0, sizeof(EggsaiProvider));
        strcpy(s_generic_provider.name, "GenericJSON");
        s_generic_provider.load_configs = generic_load_configs;
        s_generic_provider.send_message = generic_send_message;
        s_provider_initialized = true;
    }
    return &s_generic_provider;
}
