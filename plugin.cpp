/**
 * FogLAMP OutOfBound notification rule plugin
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <plugin_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <logger.h>
#include <plugin_exception.h>
#include <iostream>
#include <config_category.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <builtin_rule.h>
#include "version.h"
#include "outofbound.h"

#define RULE_NAME "OutOfBound"
#define DEFAULT_TIME_INTERVAL "30"

/**
 * Rule specific default configuration
 *
 * The "rule_config" property is a JSON object
 * with a "rules" array:
 *
 * Example:
    {
      "asset": {
        "description": "The asset name for which notifications will be generated.",
        "name": "flow"
      },
      "datapoints": [
        {
          "type": "float",
          "trigger_value": 101.3,
          "name": "random"
        }
      ],
      "evaluation_type": {
        "options": [
          "window",
          "maximum",
          "minimum",
          "average"
        ],
        "type": "enumeration",
        "description": "Rule evaluation type",
        "value": "latest"
      },
      "eval_all_datapoints": true
    }

 * If array size is greater than one, each asset with datapoint(s) is evaluated.
 * If all assets evaluations are true, then the notification is sent.
 */

#define RULE_DEFAULT_CONFIG \
			"\"description\": { " \
				"\"description\": \"Generate a notification if all configured assets trigger\", " \
				"\"type\": \"string\", " \
				"\"default\": \"Generate a notification if all configured assets trigger\", " \
				"\"displayName\" : \"Generate a notification if all configured assets trigger\", " \
				"\"order\": \"1\" }, " \
			"\"rule_config\": { " \
				"\"description\": \"The array of rules.\", " \
				"\"type\": \"JSON\", " \
				"\"default\": \"{\\\"rules\\\" : [" \
							"{ \\\"asset\\\" : {" \
								"\\\"description\\\" : \\\"The asset name for which " \
								"notifications will be generated.\\\", " \
								"\\\"name\\\" : \\\"\\\" }, " \
							   "\\\"evaluation_type\\\": {" \
								"\\\"description\\\": \\\"Rule evaluation type\\\", " \
								"\\\"type\\\": \\\"enumeration\\\", " \
									"\\\"options\\\": [ " \
										"\\\"window\\\", \\\"maximum\\\", " \
										"\\\"minimum\\\", \\\"average\\\", \\\"latest\\\" " \
									"], \\\"value\\\": \\\"latest\\\" }, " \
							   "\\\"time_window\\\": {" \
								"\\\"description\\\": \\\"Duration of the time window, in seconds, " \
								"for collecting data points except for 'latest' evaluation.\\\", " \
								"\\\"type\\\": \\\"integer\\\" , " \
								"\\\"value\\\": " DEFAULT_TIME_INTERVAL " }, " \
							   "\\\"eval_all_datapoints\\\" : true, " \
							   "\\\"datapoints\\\": [ {\\\"name\\\": \\\"\\\", " \
									"\\\"type\\\": \\\"float\\\", " \
									"\\\"trigger_value\\\": 0.0} ] } ] }\", " \
				"\"displayName\" : \"Rule configuration\", " \
				"\"order\": \"2\" }"

#define BUITIN_RULE_DESC "\"plugin\": {\"description\": \"" RULE_NAME " notification rule\", " \
			"\"type\": \"string\", \"default\": \"" RULE_NAME "\", \"readonly\": \"true\"}"

#define RULE_DEFAULT_CONFIG_INFO "{" BUITIN_RULE_DESC ", " RULE_DEFAULT_CONFIG "}"

using namespace std;

bool evalAsset(const Value& assetValue, RuleTrigger* rule);

/**
 * The C plugin interface
 */
extern "C" {
/**
 * The C API rule information structure
 */
static PLUGIN_INFORMATION ruleInfo = {
	RULE_NAME,			// Name
	VERSION,			// Version
	0,				// Flags
	PLUGIN_TYPE_NOTIFICATION_RULE,	// Type
	"1.0.0",			// Interface version
	RULE_DEFAULT_CONFIG_INFO	// Configuration
};

/**
 * Return the information about this plugin
 */
PLUGIN_INFORMATION *plugin_info()
{
	return &ruleInfo;
}

/**
 * Initialise rule objects based in configuration
 *
 * @param    config	The rule configuration category data.
 * @return		The rule handle.
 */
PLUGIN_HANDLE plugin_init(const ConfigCategory& config)
{
	
	OutOfBound* handle = new OutOfBound();
	handle->configure(config);

	return (PLUGIN_HANDLE)handle;
}

/**
 * Free rule resources
 */
void plugin_shutdown(PLUGIN_HANDLE handle)
{
	OutOfBound* rule = (OutOfBound *)handle;
	// Delete plugin handle
	delete rule;
}

/**
 * Return triggers JSON document
 *
 * @return	JSON string
 */
string plugin_triggers(PLUGIN_HANDLE handle)
{
	string ret;
	OutOfBound* rule = (OutOfBound *)handle;

	// Configuration fetch is protected by a lock
	rule->lockConfig();

	if (!rule->hasTriggers())
	{
		ret = "{\"triggers\" : []}";
		return ret;
	}

	ret = "{\"triggers\" : [ ";
	std::map<std::string, RuleTrigger *> triggers = rule->getTriggers();
	for (auto it = triggers.begin();
		  it != triggers.end();
		  ++it)
	{
		ret += "{ \"asset\"  : \"" + (*it).first + "\"";
		if (!(*it).second->getEvaluation().empty())
		{
			ret += ", \"" + (*it).second->getEvaluation() + "\" : " + \
				to_string((*it).second->getInterval()) + " }";
		}
		else
		{
			ret += " }";
		}
		
		if (std::next(it, 1) != triggers.end())
		{
			ret += ", ";
		}
	}

	ret += " ] }";

	// Release lock
	rule->unlockConfig();

	return ret;
}

/**
 * Evaluate notification data received
 *
 *  Note: all assets must trigger in order to return TRUE
 *
 * @param    assetValues	JSON string document
 *				with notification data.
 * @return			True if the rule was triggered,
 *				false otherwise.
 */
bool plugin_eval(PLUGIN_HANDLE handle,
		 const string& assetValues)
{
	Document doc;
	doc.Parse(assetValues.c_str());
	if (doc.HasParseError())
	{
		return false;
	}

	bool eval = false; 
	OutOfBound* rule = (OutOfBound *)handle;
	map<std::string, RuleTrigger *>& triggers = rule->getTriggers();

	// Iterate throgh all configured assets
	// If we have multiple asset the evaluation result is
	// TRUE only if all assets checks returned true
	for (auto t = triggers.begin();
		  t != triggers.end();
		  ++t)
	{
		string assetName = (*t).first;
		if (!doc.HasMember(assetName.c_str()))
		{
			eval = false;
		}
		else
		{
			// Get all datapoints fir assetName
			const Value& assetValue = doc[assetName.c_str()];

			// Set evaluation
			eval = evalAsset(assetValue, (*t).second);
		}
	}

	// Set final state: true is all calls to evalAsset() returned true
	rule->setState(eval);

	return eval;
}

/**
 * Return rule trigger reason: trigger or clear the notification. 
 *
 * @return	 A JSON string
 */
string plugin_reason(PLUGIN_HANDLE handle)
{
	OutOfBound* rule = (OutOfBound *)handle;

	string ret = "{ \"reason\": \"";
	ret += rule->getState() == OutOfBound::StateTriggered ? "triggered" : "cleared";
	ret += "\" }";

	return ret;
}

/**
 * Call the reconfigure method in the plugin
 *
 * Not implemented yet
 *
 * @param    newConfig		The new configuration for the plugin
 */
void plugin_reconfigure(PLUGIN_HANDLE handle,
			const string& newConfig)
{

	OutOfBound* rule = (OutOfBound *)handle;
	ConfigCategory  config("new_outofbound", newConfig);
	rule->configure(config);
}

// End of extern "C"
};

/**
 * Check whether the input datapoint
 * is a NUMBER and its value is greater than configured DOUBLE limit
 *
 * @param    point		Current input datapoint
 * @param    limitValue		The DOUBLE limit value
 * @return			True if limit is hit,
 *				false otherwise
 */
bool checkDoubleLimit(const Value& point, double limitValue)
{
	bool ret = false;

	// Check config datapoint type
	if (point.GetType() == kNumberType)
	{
		if (point.IsDouble())
		{       
			if (point.GetDouble() > limitValue)
			{       
				ret = true;
			}
		}
		else
		{
  			if (point.IsInt() ||
			    point.IsUint() ||
			    point.IsInt64() ||
			    point.IsUint64())
			{
				if (point.IsInt() ||
				    point.IsUint())
				{
					if (point.GetInt() > limitValue)
					{
						ret = true;
					}
				}
				else
				{
					if (point.GetInt64() > limitValue)
					{
						ret = true;
					}
				}
			}
		}
	}

	return ret;
}

/**
 * Evaluate datapoints values for the given asset name
 *
 * @param    assetValue		JSON object with datapoints
 * @param    rule		Current configured rule trigger.
 *
 * @return			True if evalution succeded,
 *				false otherwise.
 */
bool evalAsset(const Value& assetValue, RuleTrigger* rule)
{
	bool assetEval = false;

	bool evalAlldatapoints = rule->evalAllDatapoints();
	// Check all configured datapoints for current assetName
	vector<Datapoint *> datapoints = rule->getDatapoints();
	for (auto it = datapoints.begin();
		  it != datapoints.end();
	 	 ++it)
	{
		string datapointName = (*it)->getName();
		// Get input datapoint name
		if (assetValue.HasMember(datapointName.c_str()))
		{
			const Value& point = assetValue[datapointName.c_str()];
			// Check configuration datapoint type
			switch ((*it)->getData().getType())
			{
			case DatapointValue::T_FLOAT:
				assetEval = checkDoubleLimit(point,
							   (*it)->getData().toDouble());
				break;
			case DatapointValue::T_STRING:
			default:
				break;
				assetEval = false;
			}

			// Check eval all datapoints
			if (assetEval == true &&
			    evalAlldatapoints == false)
			{
				// At least one datapoint has been evaluated
				break;
			}
		}
		else
		{
			assetEval = false;
		}
	}

	// Return evaluation for current asset
	return assetEval;
}

/**
 * OutOfBound rule constructor
 *
 * Call parent class BuiltinRule constructor
 * passing a plugin handle
 */
OutOfBound::OutOfBound() : BuiltinRule()
{
}

/**
 * OutOfBound destructor
 */
OutOfBound::~OutOfBound()
{
}

/**
 * Configure the rule plugin
 *
 * @param    config	The configuration object to process
 */
void OutOfBound::configure(const ConfigCategory& config)
{
	string JSONrules = config.getValue("rule_config");

	Document doc;
	doc.Parse(JSONrules.c_str());

	if (!doc.HasParseError())
	{
		if (doc.HasMember("rules"))
		{
			// Gwt defined rules
			const Value& rules = doc["rules"];
			if (rules.IsArray())
			{
				// Remove current triggers
				// Configuration change is protected by a lock
				this->lockConfig();
				if (this->hasTriggers())
				{       
					this->removeTriggers();
				}       
				// Release lock
				this->unlockConfig();

				/**
				 * For each rule fetch:
				 * asset: name,
				 * evaluation_type: value
				 * time_interval
				 * datapoints array with max_allowed_value
				 * eval_all_datapoins: check all datapoint values
				 * or just eval the rule for at least one datapoint
				 */
				for (auto& rule : rules.GetArray())
				{
					if (!rule.HasMember("asset") && !rule.HasMember("datapoints"))
					{
						continue;
					}

					const Value& asset = rule["asset"];
					string assetName = asset["name"].GetString();
					if (assetName.empty())
					{
						continue;
					}
					// evaluation_type can be empty, it means latest value
					string evaluation_type;
					// time_interval might be not present only
					// if evaluation_type is empty
					unsigned int timeInterval = 0;
					if (rule.HasMember("evaluation_type"))
					{
						const Value& type = rule["evaluation_type"];
						evaluation_type = type["value"].GetString();
						if (!evaluation_type.empty() &&
						    rule.HasMember("time_interval"))
						{
							const Value& interval = rule["time_interval"];
							timeInterval = interval.GetInt();
						}
						else
						{
							// Log message
						}
					}

					const Value& datapoints = rule["datapoints"];
					bool evalAlldatapoints = true;
					bool foundDatapoints = false;
					if (rule.HasMember("eval_all_datapoints") &&
					    rule["eval_all_datapoints"].IsBool())
					{
						evalAlldatapoints = rule["eval_all_datapoints"].GetBool();
					}

					// Configuration change is protected by a lock
					this->lockConfig();

					if (this->hasTriggers())
					{
						//this->removeTriggers();
					}
					// Release lock
					this->unlockConfig();

					if (datapoints.IsArray())
					{
						for (auto& d : datapoints.GetArray())
						{
							if (d.HasMember("name"))
							{
								foundDatapoints = true;

								string dataPointName = d["name"].GetString();
								// max_allowed_value is specific for this rule
								if (d.HasMember("trigger_value") &&
								    d["trigger_value"].IsNumber())
								{
									double maxVal = d["trigger_value"].GetDouble();
									DatapointValue value(maxVal);
									Datapoint* point = new Datapoint(dataPointName, value);
									RuleTrigger* pTrigger = new RuleTrigger(dataPointName, point);
									pTrigger->addEvaluation(evaluation_type,
												timeInterval,
												evalAlldatapoints);
									
									// Configuration change is protected by a lock
									this->lockConfig();
									this->addTrigger(assetName, pTrigger);
									// Release lock
									this->unlockConfig();
								}
							}
						}
					}
					if (!foundDatapoints)
					{
						// Log message
					}
				}
			}
		}
	}
}