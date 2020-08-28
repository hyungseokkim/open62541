/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/client_subscriptions.h>

#include "open62541/namespace_robot_model_generated.h"

#include <signal.h>
#include <stdlib.h>

//historic library
#include <open62541/plugin/historydata/history_data_backend_memory.h>
#include <open62541/plugin/historydata/history_data_gathering_default.h>
#include <open62541/plugin/historydata/history_database_default.h>
#include <open62541/plugin/historydatabase.h>

UA_Boolean running = true;
static UA_NodeId conditionSource;
static UA_NodeId angleConditionInstance;

UA_Boolean tempBool = true;
UA_UInt64 callbackId;

static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}
static void
writeRandomVariable(UA_Server* server, void* data) {
    UA_NodeId myIntegerNodeId = UA_NODEID_NUMERIC(2, 6010);
    /* Write a different integer value */
    UA_Int32 myInteger = UA_UInt32_random() % 360;
    UA_Variant myVar;
    UA_Variant_init(&myVar);
    UA_Variant_setScalar(&myVar, &myInteger, &UA_TYPES[UA_TYPES_INT32]);
    UA_Server_writeValue(server, myIntegerNodeId, myVar);
}
static void
dataChangeNotificationCallback(UA_Server* server, UA_UInt32 monitoredItemId,
    void* monitoredItemContext, const UA_NodeId* nodeId,
    void* nodeContext, UA_UInt32 attributeId,
    const UA_DataValue* value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
        "Received Notification");
}
static void
addMonitoredItem(UA_Server* server) {
    UA_NodeId MonitoredItemNodeId = UA_NODEID_NUMERIC(2, 6010);
    UA_MonitoredItemCreateRequest monRequest =
        UA_MonitoredItemCreateRequest_default(MonitoredItemNodeId);
    monRequest.requestedParameters.samplingInterval = 3000.0; /*
    1 s interval */
    UA_Server_createDataChangeMonitoredItem(server, UA_TIMESTAMPSTORETURN_SOURCE,
        monRequest, NULL, dataChangeNotificationCallback);
}
static UA_StatusCode
addConditionSourceObject(UA_Server* server) {
    UA_ObjectAttributes object_attr = UA_ObjectAttributes_default;
    object_attr.eventNotifier = 1;

    object_attr.displayName = UA_LOCALIZEDTEXT("en", "ConditionSourceObject");
    UA_StatusCode retval = UA_Server_addObjectNode(server, UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(0, "ConditionSourceObject"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        object_attr, NULL, &conditionSource);

    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "creating Condition Source failed. StatusCode %s", UA_StatusCode_name(retval));
    }
	retval = UA_Server_addReference(server, UA_NODEID_NUMERIC(2, 5004),//Robot1 Instance
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASNOTIFIER),
        UA_EXPANDEDNODEID_NUMERIC(conditionSource.namespaceIndex,
            conditionSource.identifier.numeric),
        UA_TRUE);

    return retval;
}
static UA_StatusCode
addAngleCondition(UA_Server* server) {  // Condition 생성
    UA_StatusCode retval = addConditionSourceObject(server);    // Condition source object 생성
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "creating Condition Source failed. StatusCode %s", UA_StatusCode_name(retval)); // 생성 오류시 로그 출력
    }
    retval = UA_Server_createCondition(server,  // Condition source object 하위에 Condition instance 생성
        UA_NODEID_NULL, UA_NODEID_NUMERIC(0, UA_NS0ID_OFFNORMALALARMTYPE),
        UA_QUALIFIEDNAME(0, "angleCondition"), conditionSource,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), &angleConditionInstance);

    return retval;
}
static void
afterWriteCallbackAngleNode(UA_Server* server,
    const UA_NodeId* sessionId, void* sessionContext,
    const UA_NodeId* nodeId, void* nodeContext,
    const UA_NumericRange* range, const UA_DataValue* data) {


    UA_QualifiedName activeStateField = UA_QUALIFIEDNAME(0, "ActiveState");
    UA_QualifiedName messageField = UA_QUALIFIEDNAME(0, "Message");
    UA_QualifiedName idField = UA_QUALIFIEDNAME(0, "Id");

    UA_NodeId AngleNode = UA_NODEID_NUMERIC(2, 6010);
    UA_NodeId StatusNode = UA_NODEID_NUMERIC(2, 6007);

    UA_StatusCode retval = UA_Server_writeObjectProperty_scalar(server, angleConditionInstance, UA_QUALIFIEDNAME(0, "Time"),
        &data->serverTimestamp, &UA_TYPES[UA_TYPES_DATETIME]);
    UA_Variant value;
    UA_Boolean idValue = false;
    UA_Variant_setScalar(&value, &idValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
    retval |= UA_Server_setConditionVariableFieldProperty(server, angleConditionInstance,
        &value, activeStateField, idField);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Setting ActiveState/Id Field failed. StatusCode %s", UA_StatusCode_name(retval));
        return;
    }


    if (*(UA_Int32*)(data->value.data) > 300) {
        UA_Server_removeCallback(server, callbackId);

        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "over 300 %d", *(UA_Int32*)(data->value.data));
        UA_Boolean robotStatus = false;
        UA_Variant_setScalar(&value, &robotStatus, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = UA_Server_writeValue(server, StatusNode, value);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changing status node failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }
        UA_Int32 robotAngle = 0;
        UA_Variant_setScalar(&value, &robotAngle, &UA_TYPES[UA_TYPES_INT32]);
        retval = UA_Server_writeValue(server, AngleNode, value);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changing Angle node failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }
        UA_LocalizedText messageValue = UA_LOCALIZEDTEXT("en", "Danger & Stop");
        UA_Variant_setScalar(&value, &messageValue, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = UA_Server_setConditionField(server, angleConditionInstance,
            &value, messageField);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Setting Message Field failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }

        retval = UA_Server_triggerConditionEvent(server, angleConditionInstance, conditionSource, NULL);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Triggering condition event failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }
    }

    else if (*(UA_Int32*)(data->value.data) > 200) {
        UA_LocalizedText messageValue = UA_LOCALIZEDTEXT("en", "Warning");
        UA_Variant_setScalar(&value, &messageValue, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
        retval = UA_Server_setConditionField(server, angleConditionInstance,
            &value, messageField);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Setting Message Field failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }

        retval = UA_Server_triggerConditionEvent(server, angleConditionInstance, conditionSource, NULL);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Triggering condition event failed. StatusCode %s", UA_StatusCode_name(retval));
            return;
        }
    }

}

onOffMethodCallback(UA_Server* server,
    const UA_NodeId* sessionId, void* sessionHandle,
    const UA_NodeId* methodId, void* methodContext,
    const UA_NodeId* objectId, void* objectContext,
    size_t inputSize, const UA_Variant* input,
    size_t outputSize, UA_Variant* output) {
    UA_NodeId AngleNode = UA_NODEID_NUMERIC(2, 6010);
    UA_NodeId StatusNode = UA_NODEID_NUMERIC(2, 6007);


    UA_Variant value;
    UA_StatusCode retval;

    UA_ValueCallback callback;
    callback.onRead = NULL;
    callback.onWrite = afterWriteCallbackAngleNode;


    if (tempBool == true) {

        retval = UA_Server_setVariableNode_valueCallback(server, AngleNode, callback);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "setting AngleNode Callback failed. StatusCode %s", UA_StatusCode_name(retval));
            return retval;
        }
        UA_Boolean robotStatus = true;
        UA_Variant_setScalar(&value, &robotStatus, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = UA_Server_writeValue(server, StatusNode, value);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changing status node failed. StatusCode %s", UA_StatusCode_name(retval));
            return retval;
        }
        UA_Server_addRepeatedCallback(server, writeRandomVariable, NULL, 5000, &callbackId);  // 10초에 한 번씩 writeRandomVariable 함수 호출
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Robot is turned on");

        tempBool = false;
    }
    else {
        UA_Server_removeCallback(server, callbackId);

        UA_Boolean robotStatus = false;
        UA_Variant_setScalar(&value, &robotStatus, &UA_TYPES[UA_TYPES_BOOLEAN]);
        retval = UA_Server_writeValue(server, StatusNode, value);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changing status node failed. StatusCode %s", UA_StatusCode_name(retval));
            return retval;
        }

        UA_Int32 robotAnlge = 0;
        UA_Variant_setScalar(&value, &robotAnlge, &UA_TYPES[UA_TYPES_INT32]);
        retval = UA_Server_writeValue(server, AngleNode, value);
        if (retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changing status node failed. StatusCode %s", UA_StatusCode_name(retval));
            return retval;
        }
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Robot is turned off");

        tempBool = true;
    }

    return UA_STATUSCODE_GOOD;
}
static void
addRobotStatusMethod(UA_Server* server) {

    UA_MethodAttributes onOffAttr = UA_MethodAttributes_default;
    onOffAttr.description = UA_LOCALIZEDTEXT("en-US", "On/Off status");
    onOffAttr.displayName = UA_LOCALIZEDTEXT("en-US", "On/Off");
    onOffAttr.executable = true;
    onOffAttr.userExecutable = true;
    UA_Server_addMethodNode(server, UA_NODEID_NUMERIC(1, 10000),
        conditionSource,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, "On/Off"),
        onOffAttr, &onOffMethodCallback,
        0, NULL, 0, NULL, NULL, NULL);
}
static UA_StatusCode
setUpEnvironment(UA_Server* server) {
  
    UA_ValueCallback callback;
    callback.onRead = NULL;
    
    UA_StatusCode retval = addAngleCondition(server);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "adding condition failed. StatusCode %s", UA_StatusCode_name(retval));
        return retval;
    }
       
    
    addRobotStatusMethod(server);
    return retval;
}


int main(int argc, char** argv) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);
    
    UA_Server *server = UA_Server_new();

    UA_ServerConfig* config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);

    UA_StatusCode retval;

    UA_HistoryDataGathering gathering = UA_HistoryDataGathering_Default(1);
    config->historyDatabase = UA_HistoryDatabase_default(gathering);
    UA_NodeId historicNodeId = UA_NODEID_NUMERIC(2, 6010);
    /* create nodes from nodeset */
    if(namespace_robot_model_generated(server) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Could not add the example nodeset. "
        "Check previous output for any error.");
        retval = UA_STATUSCODE_BADUNEXPECTEDERROR;
    } else {
        /************* Historic Access ***************/
        UA_HistorizingNodeIdSettings setting;

        setting.historizingBackend = UA_HistoryDataBackend_Memory(3, 100);
        setting.maxHistoryDataResponseSize = 100;
        setting.historizingUpdateStrategy = UA_HISTORIZINGUPDATESTRATEGY_VALUESET;

        retval = gathering.registerNodeId(server, gathering.context, &historicNodeId, setting);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "registerNodeId %s", UA_StatusCode_name(retval));

        addMonitoredItem(server);

        setUpEnvironment(server);
        
        retval = UA_Server_run(server, &running);
    }

    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
