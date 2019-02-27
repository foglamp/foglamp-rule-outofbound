#ifndef _OVERMAX_RULE_H
#define _OVERMAX_RULE_H
/*
 * FogLAMP OutOfBound class
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <plugin.h>
#include <plugin_manager.h>
#include <config_category.h>
#include <rule_plugin.h>
#include <builtin_rule.h>

/**
 * OutOfBound class, derived from Notification BuiltinRule
 */
class OutOfBound: public BuiltinRule
{
	public:
		OutOfBound();
		~OutOfBound();

		void	configure(const ConfigCategory& config);
		void	lockConfig() { m_configMutex.lock(); };
		void	unlockConfig() { m_configMutex.unlock(); };

	private:
		std::mutex	m_configMutex;	
};

#endif
