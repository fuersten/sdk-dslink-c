#include <broker/upstream/upstream_node.h>
#include <broker/utils.h>
#include <dslink/utils.h>

#define LOG_TAG "upstream"
#include <dslink/log.h>


void add_upstream_invoke(RemoteDSLink *link,
                         BrokerNode *node,
                         json_t *req);

static
void save_upstream_node(BrokerNode *node) {
    char tmp[128];
    char* escname = dslink_str_escape(node->name);
    sprintf(tmp, "upstream/%s", escname);
    json_t *output = json_object();

    BrokerNode* propNode;
    propNode = dslink_map_get(node->children, "name")->data;
    json_object_set(output, "name", propNode->value);

    propNode = dslink_map_get(node->children, "brokerName")->data;
    json_object_set(output, "brokerName", propNode->value);

    propNode = dslink_map_get(node->children, "url")->data;
    json_object_set(output, "url", propNode->value);

    propNode = dslink_map_get(node->children, "token")->data;
    json_object_set(output, "token", propNode->value);

    propNode = dslink_map_get(node->children, "enabled")->data;
    json_object_set(output, "enabled", propNode->value);

    json_dump_file(output, tmp, 0);
    json_decref(output);
    dslink_free(escname);
}

static
void load_upstream_node(BrokerNode *parentNode, const char* nodeName, json_t* data) {
    char* unescName = dslink_str_escape(nodeName);
    BrokerNode *upstreamNode = broker_node_create(nodeName, "node");
    dslink_free(unescName);
    if (!upstreamNode) {
        return;
    }
    add_upstream_invoke(NULL, parentNode, data);

}


static
int load_upstreams(BrokerNode *parentNode){
    uv_fs_t dir;

    uv_fs_mkdir(NULL, &dir, "upstream", 0770, NULL);

    if (uv_fs_scandir(NULL, &dir, "upstream", 0, NULL) < 0) {
        return 0;
    }

    uv_dirent_t d;
    while (uv_fs_scandir_next(&dir, &d) != UV_EOF) {
        if (d.type != UV_DIRENT_FILE) {
            continue;
        }

        char tmp[256];
        int len = snprintf(tmp, sizeof(tmp) - 1, "upstream/%s", d.name);
        tmp[len] = '\0';

        json_error_t err;
        json_t *val = json_load_file(tmp, 0 , &err);
        if (val) {
            load_upstream_node(parentNode, d.name, val);
            json_decref(val);
        }
    }
    return 0;
}


static
void delete_upstream_invoke(RemoteDSLink *link,
                         BrokerNode *node,
                         json_t *req) {
    broker_utils_send_closed_resp(link, req, NULL);

    BrokerNode *parentNode = node->parent;

    char* escname = dslink_str_escape(parentNode->name);
    char tmp[256];
    int len = snprintf(tmp, sizeof(tmp) - 1, "upstream/%s", escname);
    tmp[len] = '\0';
    dslink_free(escname);

    uv_fs_t unlink_req;
    uv_fs_unlink(NULL, &unlink_req, tmp, NULL);


    broker_node_free(parentNode);
}




void add_upstream_invoke(RemoteDSLink *link,
                      BrokerNode *node,
                      json_t *req) {
    json_t *params = NULL;
    BrokerNode* parentNode;
    if (link) {
        // invoked
        if (req) {
            params = json_object_get(req, "params");
        }
        if (!json_is_object(params)) {
            broker_utils_send_closed_resp(link, req, "invalidParameter");
            return;
        }
        parentNode = node->parent;
    } else {
        // loaded
        params = req;
        parentNode = node;
    }

    json_t* namejson = json_object_get(params , "name");
    json_t* brokerNameJson = json_object_get(params , "brokerName");
    json_t* urlJson = json_object_get(params , "url");
    json_t* tokenJson = json_object_get(params , "token");
    json_t* enabledJson = json_object_get(params , "enabled");

    if (!json_is_string(namejson) || !json_is_string(brokerNameJson) || !json_is_string(urlJson)) {
        broker_utils_send_closed_resp(link, req, "invalidParameter");
        return;
    }
    const char *name = json_string_value(namejson);

    if (!parentNode || dslink_map_contains(parentNode->children, (void*)name)) {
        broker_utils_send_closed_resp(link, req, "invalidParameter");
        return;
    }

    BrokerNode *upstreamNode = broker_node_create(name, "node");
    if (!upstreamNode) {
        return;
    }

    if (broker_node_add(parentNode, upstreamNode) != 0) {
        goto fail;
    }

    BrokerNode *propNode;
    propNode = broker_node_create("name", "node");
    json_object_set_new(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, namejson, 0);
    broker_node_add(upstreamNode, propNode);

    propNode = broker_node_create("brokerName", "node");
    json_object_set_new(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, brokerNameJson, 0);
    broker_node_add(upstreamNode, propNode);


    propNode = broker_node_create("url", "node");
    json_object_set_new(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, urlJson, 0);
    broker_node_add(upstreamNode, propNode);

    propNode = broker_node_create("token", "node");
    json_object_set_new(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, tokenJson, 0);
    broker_node_add(upstreamNode, propNode);


    propNode = broker_node_create("enabled", "node");
    json_object_set_new(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new(propNode->meta, "$type", json_string_nocheck("bool"));
    if (json_is_false(enabledJson)) {
        broker_node_update_value(propNode, json_false(), 0);
    } else {
        broker_node_update_value(propNode, json_true(), 0);
    }
    broker_node_add(upstreamNode, propNode);

    // TODO detect enabled change and start/stop upstream

    BrokerNode *deleteAction = broker_node_create("delete", "node");
    json_object_set_new(deleteAction->meta, "$invokable", json_string_nocheck("config"));
    broker_node_add(upstreamNode, deleteAction);

    deleteAction->on_invoke = delete_upstream_invoke;

    log_info("Upstream added `%s`\n", name);
    if (link) {
        // only save when it's from an action
        save_upstream_node(upstreamNode);
    }


    return;
fail:
    broker_node_free(upstreamNode);
}

int init_sys_upstream_node(BrokerNode *sysNode) {

    BrokerNode *upstreamNode = broker_node_create("upstream", "node");
    if (!upstreamNode) {
        return 1;
    }

    if (broker_node_add(sysNode, upstreamNode) != 0) {
        broker_node_free(upstreamNode);
        return 1;
    }


    BrokerNode *addUpstreamAction = broker_node_create("add_connection", "node");
    if (!addUpstreamAction) {
        return 1;
    }

    if (json_object_set_new(addUpstreamAction->meta, "$invokable",
                            json_string("config")) != 0) {
        broker_node_free(addUpstreamAction);
        return 1;
    }


    json_error_t err;
    json_t *paramList = json_loads(
            "[{\"name\":\"name\",\"type\":\"string\",\"description\":\"Upstream Broker Name\",\"placeholder\":\"UpstreamBroker\"},{\"name\":\"url\",\"type\":\"string\",\"description\":\"Url to the Upstream Broker\",\"placeholder\":\"http://upstream.broker.com/conn\"},{\"name\":\"brokerName\",\"type\":\"string\",\"description\":\"The name of the link when connected to the Upstream Broker\",\"placeholder\":\"ThisBroker\"},{\"name\":\"token\",\"type\":\"string\",\"description\":\"Broker Token (if needed)\",\"placeholder\":\"OptionalAuthToken\"}]",
            0, &err);
    if (!paramList || json_object_set_new(addUpstreamAction->meta, "$params", paramList) != 0) {
        return 1;
    }


    if (broker_node_add(upstreamNode, addUpstreamAction) != 0) {
        broker_node_free(addUpstreamAction);
        return 1;
    }

    addUpstreamAction->on_invoke = add_upstream_invoke;

    load_upstreams(upstreamNode);
    return 0;
}