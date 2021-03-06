/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

/**
 * Using the nodestore switch plugin
 * ------------------------
 * Is only compiled with: UA_ENABLE_CUSTOM_NODESTORE and UA_ENABLE_NODESTORE_SWITCH
 * 
 * Adds the nodestore switch as plugin into the server and demonstrates its use with a second default nodestore.
 *
 * The nodestore switch links namespace indices to nodestores. So that every access to a node is redirected, based on its namespace index.
 * The linking between nodestores and and namespaces may be altered during runtime (With the use of UA_Server_run_iterate for example).
 * This enables the user to have a persistent and different stores for nodes (for example in a database or file) or to transform objects into OPC UA nodes.
 * Backup scenarios are also possible.
 */

#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/nodestore.h>
#include <open62541/plugin/nodestore_switch.h>
#include <open62541/util.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <signal.h>
#include <stdlib.h>

static const UA_NodeId baseDataVariableType = { 0, UA_NODEIDTYPE_NUMERIC, {
		UA_NS0ID_BASEDATAVARIABLETYPE } };

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
	UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
	running = false;
}

static void addVariableNode(UA_Server* server, UA_UInt16 nsIndex, char* name) {
	/* add a static variable node to the server */
	UA_VariableAttributes myVar = UA_VariableAttributes_default;
	myVar.description = UA_LOCALIZEDTEXT("en-US",
			"This node lives in a separate nodestore.");
	myVar.displayName = UA_LOCALIZEDTEXT("en-US", name);
	myVar.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
	myVar.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
	myVar.valueRank = UA_VALUERANK_SCALAR;
	UA_Int32 myInteger = 42;
	UA_Variant_setScalar(&myVar.value, &myInteger, &UA_TYPES[UA_TYPES_INT32]);
	const UA_QualifiedName myIntegerName = UA_QUALIFIEDNAME(nsIndex, name);
	const UA_NodeId myIntegerNodeId = UA_NODEID_STRING(nsIndex, name);
	UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
	UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
	UA_Server_addVariableNode(server, myIntegerNodeId, parentNodeId,
			parentReferenceNodeId, myIntegerName, baseDataVariableType, myVar,
			NULL, NULL);
}

static void printNode(void* visitorContext, UA_Node* node) {
	UA_String nodeIdString = UA_STRING_NULL;
	UA_StatusCode result = UA_NodeId_toString(&node->nodeId, &nodeIdString);
	if (result != UA_STATUSCODE_GOOD) {
		UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
				"Could not convert nodeId.");
	} else {
		UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s%.*s",
				(char*) visitorContext, UA_PRINTF_STRING_DATA(nodeIdString));
	}
}

int main(void) {
	signal(SIGINT, stopHandler);
	signal(SIGTERM, stopHandler);

	UA_Server *server = UA_Server_new();
	UA_ServerConfig_setDefault(UA_Server_getConfig(server));

	/*
	 * add a second default nodestore for NS1
	 * Get the nodestore switch from the server. (Only possible with UA_ENABLE_CUSTOM_NODESTORE)
	 * Can be casted to a nodestoreSwitch if UA_ENABLE_NODESTORE_SWITCH is set, so that the nodestore switch is used as plugin
	 */
	UA_Nodestore_Switch* nodestoreSwitch =
			(UA_Nodestore_Switch*) UA_Server_getNodestore(server);
	// Create a default nodestore as an own nodestore for namespace 1 (application namespace) and remember a pointer to its interface
	UA_NodestoreInterface * ns1Nodestore = NULL;
	UA_StatusCode retval = UA_Nodestore_Default_Interface_new(&ns1Nodestore);
	if (retval != UA_STATUSCODE_GOOD) {
		UA_Server_delete(server);
		return EXIT_FAILURE;
	}
	// Link the ns1Nodestore to namespace 1, so that all nodes created in namespace 1 reside in ns1Nodestore
	UA_Nodestore_Switch_setNodestore(nodestoreSwitch, 1, ns1Nodestore);

	// Add some test nodes to namespace 1
	addVariableNode(server, 1, "TestNode1");
	addVariableNode(server, 1, "TestNode2");
	addVariableNode(server, 1, "TestNode3");
	// Start server and run till SIGINT or SIGTERM
	UA_Server_run(server, &running);

	// Unlink nodestore for namespace 1
	UA_Nodestore_Switch_setNodestore(nodestoreSwitch, 1, NULL);
	// Shutdown server --> normaly the nodestore and all nodes in it get deleted, but it is unlinked
	UA_Server_delete(server);

	// At this point the nodestore 1 could be stored in a memory mapped file or database
	// This or another application could load the nodestore
	// In general the lifecycle of the nodestore is unlinked from the lifecycle of the server
	// Print all nodes in the nodestore
	ns1Nodestore->iterate(ns1Nodestore->context,
			(UA_NodestoreVisitor) printNode, "Found Node in NS1: ");

	// Startup a new server with the old nodestore in namespace1
	server = UA_Server_new();
	UA_ServerConfig_setDefault(UA_Server_getConfig(server));
	nodestoreSwitch = (UA_Nodestore_Switch*) UA_Server_getNodestore(server);
	// Link ns1Nodestore
	UA_Nodestore_Switch_setNodestore(nodestoreSwitch, 1, ns1Nodestore);
	running = UA_TRUE;
	UA_Server_run(server, &running);

	// Delete the second server together with its nodestore and nodes
	UA_Server_delete(server);

	return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
