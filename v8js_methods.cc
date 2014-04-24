/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | http://www.opensource.org/licenses/mit-license.php  MIT License      |
  +----------------------------------------------------------------------+
  | Author: Jani Taskinen <jani.taskinen@iki.fi>                         |
  | Author: Patrick Reilly <preilly@php.net>                             |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_v8js_macros.h"

extern "C" {
#include "zend_exceptions.h"
}

/* Forward declarations */
void php_v8js_amd_normalise_id(const char *base_module_id, const char *module_id, char *normalised_module_id);
void php_v8js_run_code(php_v8js_ctx *c, const char *source);

static void _php_v8js_dumper(v8::Isolate *isolate, v8::Local<v8::Value> var, int level TSRMLS_DC) /* {{{ */
{
	if (level > 1) {
		php_printf("%*c", (level - 1) * 2, ' ');
	}

	if (var.IsEmpty())
	{
		php_printf("<empty>\n");
		return;
	}
	if (var->IsNull() || var->IsUndefined() /* PHP compat */)
	{
		php_printf("NULL\n");
		return;
	}
	if (var->IsInt32())
	{
		php_printf("int(%ld)\n", (long) var->IntegerValue());
		return;
	}
	if (var->IsUint32())
	{
		php_printf("int(%lu)\n", (unsigned long) var->IntegerValue());
		return;
	}
	if (var->IsNumber())
	{
		php_printf("float(%f)\n", var->NumberValue());
		return;
	}
	if (var->IsBoolean())
	{
		php_printf("bool(%s)\n", var->BooleanValue() ? "true" : "false");
		return;
	}

	v8::TryCatch try_catch; /* object.toString() can throw an exception */
	v8::Local<v8::String> details = var->ToDetailString();
	if (try_catch.HasCaught()) {
		details = V8JS_SYM("<toString threw exception>");
	}
	v8::String::Utf8Value str(details);
	const char *valstr = ToCString(str);
	size_t valstr_len = details->ToString()->Utf8Length();

	if (var->IsString())
	{
		php_printf("string(%zu) \"", valstr_len, valstr);
		PHPWRITE(valstr, valstr_len);
		php_printf("\"\n");
	}
	else if (var->IsDate())
	{
		// fake the fields of a PHP DateTime
		php_printf("Date(%s)\n", valstr);
	}
#if PHP_V8_API_VERSION >= 2003007
	else if (var->IsRegExp())
	{
		php_printf("regexp(%s)\n", valstr);
	}
#endif
	else if (var->IsArray())
	{
		v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(var);
		uint32_t length = array->Length();

		php_printf("array(%d) {\n", length);

		for (unsigned i = 0; i < length; i++) {
			php_printf("%*c[%d] =>\n", level * 2, ' ', i);
			_php_v8js_dumper(isolate, array->Get(i), level + 1 TSRMLS_CC);
		}

		if (level > 1) {
			php_printf("%*c", (level - 1) * 2, ' ');
		}

		ZEND_PUTS("}\n");
	}
	else if (var->IsObject())
	{
		v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(var);
		V8JS_GET_CLASS_NAME(cname, object);
		int hash = object->GetIdentityHash();

		if (var->IsFunction())
		{
			v8::String::Utf8Value csource(object->ToString());
			php_printf("object(Closure)#%d {\n%*c%s\n", hash, level * 2 + 2, ' ', ToCString(csource));
		}
		else
		{
			v8::Local<v8::Array> keys = object->GetOwnPropertyNames();
			uint32_t length = keys->Length();

			if (strcmp(ToCString(cname), "Array") == 0 ||
				strcmp(ToCString(cname), "V8Object") == 0) {
				php_printf("array");
			} else {
				php_printf("object(%s)#%d", ToCString(cname), hash);
			}
			php_printf(" (%d) {\n", length);

			for (unsigned i = 0; i < length; i++) {
				v8::Local<v8::String> key = keys->Get(i)->ToString();
				v8::String::Utf8Value kname(key);
				php_printf("%*c[\"%s\"] =>\n", level * 2, ' ', ToCString(kname));
				_php_v8js_dumper(isolate, object->Get(key), level + 1 TSRMLS_CC);
			}
		}

		if (level > 1) {
			php_printf("%*c", (level - 1) * 2, ' ');
		}

		ZEND_PUTS("}\n");
	}
	else /* null, undefined, etc. */
	{
		php_printf("<%s>\n", valstr);
	}
}

void php_v8js_run_code(php_v8js_ctx *c, const char *source, const char *script_name)
{
	v8::Isolate *isolate = c->isolate;

	// Each module gets its own global context so different modules do not affect each other
	v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
	php_v8js_register_methods(global, c);
	v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, v8::Context::New(isolate, NULL, global));
	v8::Context::Scope scope(context);

	// Create and parse the script
	v8::Local<v8::Script> script = v8::Script::New(v8::String::New(source), v8::String::New(script_name));

	// If the script is empty then there must be syntax errors
	if (script.IsEmpty()) {
		v8::ThrowException(V8JS_SYM("Script syntax error"));
		return;
	}

	script->Run();
}

bool php_v8js_module_in_map(std::map<const char *, v8js_module_t> modules, const char *key)
{
	for (std::map<const char *, v8js_module_t>::iterator it = modules.begin(); it != modules.end(); ++it) {
		if (strcmp(it->first, key) == 0) {
			return true;
		}
	}

	return false;
}

bool php_v8js_string_in_vector(std::vector<const char *> vector, const char *check)
{
	for (std::vector<const char *>::iterator it = vector.begin(); it != vector.end(); ++it) {
		if (strcmp(*it, check) == 0) {
			return true;
		}
	}

	return false;
}

v8::Handle<v8::Value> php_v8js_get_module_from_map(std::map<const char *, v8js_module_t> modules, const char *key, v8::Isolate *isolate)
{
	for (std::map<const char *, v8js_module_t>::iterator it = modules.begin(); it != modules.end(); ++it) {
		if (strcmp(it->first, key) == 0) {
			return v8::Local<v8::Value>::New(isolate, it->second);
		}
	}

	return V8JS_UNDEFINED;
}

void php_v8js_load_module(php_v8js_ctx *c, char *normalised_module_id)
{
	V8JS_TSRMLS_FETCH();

	v8::Isolate *isolate = c->isolate;

    // Invoke module loader callable

	zval *source_zend;
	zval *normalised_module_id_zend;

	MAKE_STD_ZVAL(normalised_module_id_zend);
	ZVAL_STRING(normalised_module_id_zend, normalised_module_id, 1);

	zval **params[1] = {&normalised_module_id_zend};

	zend_try {
		if (FAILURE == call_user_function_ex(EG(function_table), NULL, c->module_loader, &source_zend, 1, params, 0, NULL TSRMLS_CC)) {
			v8::ThrowException(V8JS_SYM("Module loader callback failed"));
			return;
		}
	} zend_catch {
		v8::ThrowException(V8JS_SYM("Module loader terminated execution"));
		return;
	} zend_end_try();

	// Check if a PHP exception was thrown
	if (EG(exception)) {
		zend_clear_exception(TSRMLS_C);
		v8::ThrowException(V8JS_SYM("Module loader failed"));
		return;
	}

	// Convert the return value to string
	if (Z_TYPE_P(source_zend) != IS_STRING) {
		convert_to_string(source_zend);
	}

	// Check that some code has been returned
	if (Z_STRLEN_P(source_zend) == 0) {
		v8::ThrowException(V8JS_SYM("Module loader callback did not return code"));
		return;
	}

	const char *source_str = Z_STRVAL_P(source_zend);

	// Run the Javascript code

	v8::TryCatch try_catch;

	c->modules_stack.push_back(normalised_module_id);
	php_v8js_run_code(c, source_str, normalised_module_id);
	c->modules_stack.pop_back();

	if (try_catch.HasCaught()) {
		try_catch.ReThrow();
		return;
	}

	if (!php_v8js_module_in_map(V8JSG(modules_loaded), normalised_module_id)) {
		v8::ThrowException(V8JS_SYM("Module not loaded"));
		return;
	}
}

/* global.exit - terminate execution */
V8JS_METHOD(exit) /* {{{ */
{
	v8::V8::TerminateExecution();
}
/* }}} */

/* global.sleep - sleep for passed seconds */
V8JS_METHOD(sleep) /* {{{ */
{
	php_sleep(info[0]->Int32Value());
}
/* }}} */

/* global.print - php print() */
V8JS_METHOD(print) /* {{{ */
{
	v8::Isolate *isolate = info.GetIsolate();
	int ret = 0;
	V8JS_TSRMLS_FETCH();

	for (int i = 0; i < info.Length(); i++) {
		v8::String::Utf8Value str(info[i]);
		const char *cstr = ToCString(str);
		ret = PHPWRITE(cstr, strlen(cstr));
	}
	info.GetReturnValue().Set(V8JS_INT(ret));
}
/* }}} */

/* global.var_dump - Dump JS values */
V8JS_METHOD(var_dump) /* {{{ */
{
	v8::Isolate *isolate = info.GetIsolate();
	V8JS_TSRMLS_FETCH();

	for (int i = 0; i < info.Length(); i++) {
		_php_v8js_dumper(isolate, info[i], 1 TSRMLS_CC);
	}

	info.GetReturnValue().Set(V8JS_NULL);
}
/* }}} */

V8JS_METHOD(define)
{
	V8JS_TSRMLS_FETCH();

	// Get the extension context
	v8::Handle<v8::External> data = v8::Handle<v8::External>::Cast(info.Data());
	php_v8js_ctx *c = static_cast<php_v8js_ctx*>(data->Value());

	v8::Isolate *isolate = c->isolate;

	v8::Handle<v8::Array> dependencies;

	// Used to move through the arguments depending on what is passed
	// The dependency array is optional i.e. define(function() ...) and define([], function() ...) are identical
	int arg = 0;

	// If we have a module identifier then ignore it
	if (info[arg]->IsString()) {
		arg++;
	}

	if (info[arg]->IsArray()) {
		// We have an array that we can assume is a set of dependencies
		dependencies = v8::Handle<v8::Array>::Cast(info[arg]);

		// Move to the next argument
		++arg;
	} else {
		// No dependencies specified
		dependencies = v8::Array::New(arg);
	}

	v8::Handle<v8::Function> factory;

	if (info[arg]->IsFunction()) {
		// We have a function that we can assume is a factory method
		factory = v8::Handle<v8::Function>::Cast(info[arg]);
	} else {
		info.GetReturnValue().Set(v8::ThrowException(V8JS_SYM("No factory method")));
		return;
	}

	v8::Handle<v8::Value> args[dependencies->Length()];

	std::vector<const char *> current_modules;

	for (int i = 0; i < dependencies->Length(); ++i)
	{
		v8::String::Utf8Value module_id_v8(dependencies->Get(i));
		const char *module_id = ToCString(module_id_v8);

		// Normalise the module ID
		char *normalised_module_id = (char *)emalloc(PATH_MAX);
		php_v8js_amd_normalise_id(c->modules_stack.back(), module_id, normalised_module_id);

		// Check for module cyclic dependencies
		if (std::count(c->modules_stack.begin(), c->modules_stack.end(), normalised_module_id) > 0) {
			info.GetReturnValue().Set(v8::ThrowException(V8JS_SYM("Module cyclic dependency")));
			return;
	    }

    	php_v8js_load_module(c, normalised_module_id);

    	current_modules.push_back(normalised_module_id);

    	// Build the dependency arguments to be passed to the factory functin
    	args[i] = v8::Local<v8::Value>::New(isolate, php_v8js_get_module_from_map(V8JSG(modules_loaded), normalised_module_id, isolate));
	}

	v8::Handle<v8::Value> dependency;

	c->current_modules = current_modules;

	v8::TryCatch try_catch;

	if (dependencies->Length() > 0) {
		dependency = factory->Call(factory, dependencies->Length(), args);
	} else {
		dependency = factory->Call(factory, 0, NULL);
	}

    if (try_catch.HasCaught()) {
		try_catch.ReThrow();
		return;
    }

	if (c->modules_stack.size() > 1) {
		// Store the persistent module
		v8js_module_t *persist_module;
		persist_module = &V8JSG(modules_loaded)[c->modules_stack.back()];
		persist_module->Reset(isolate, dependency);
	}
}

V8JS_METHOD(require)
{
	V8JS_TSRMLS_FETCH();

	// Get the extension context
	v8::Handle<v8::External> data = v8::Handle<v8::External>::Cast(info.Data());
	php_v8js_ctx *c = static_cast<php_v8js_ctx*>(data->Value());

	v8::Isolate *isolate = c->isolate;

	v8::String::Utf8Value module_id_v8(info[0]);
	const char *module_id = ToCString(module_id_v8);

	// Normalise the module ID
	char *normalised_module_id = (char *)emalloc(PATH_MAX);
	php_v8js_amd_normalise_id(c->modules_stack.back(), module_id, normalised_module_id);

	// Check if the required module is available in this scope
	if (!php_v8js_string_in_vector(c->current_modules, normalised_module_id)) {
		info.GetReturnValue().Set(v8::ThrowException(V8JS_SYM("Required module not available in this scope")));
		return;
	}

	if (!php_v8js_module_in_map(V8JSG(modules_loaded), normalised_module_id)) {
		info.GetReturnValue().Set(v8::ThrowException(V8JS_SYM("Required module not loaded")));
    	return;
   	}

	// Return the module
	info.GetReturnValue().Set(php_v8js_get_module_from_map(V8JSG(modules_loaded), normalised_module_id, isolate));
}

void php_v8js_register_methods(v8::Handle<v8::ObjectTemplate> global, php_v8js_ctx *c) /* {{{ */
{
	v8::Isolate *isolate = c->isolate;

	global->Set(V8JS_SYM("exit"), v8::FunctionTemplate::New(V8JS_MN(exit)), v8::ReadOnly);
	global->Set(V8JS_SYM("sleep"), v8::FunctionTemplate::New(V8JS_MN(sleep)), v8::ReadOnly);
	global->Set(V8JS_SYM("print"), v8::FunctionTemplate::New(V8JS_MN(print)), v8::ReadOnly);
	global->Set(V8JS_SYM("var_dump"), v8::FunctionTemplate::New(V8JS_MN(var_dump)), v8::ReadOnly);

	global->Set(V8JS_SYM("require"), v8::FunctionTemplate::New(V8JS_MN(require), v8::External::New(c)), v8::ReadOnly);

	// The define function needs to have an amd property
	v8::Local<v8::FunctionTemplate> define = v8::FunctionTemplate::New(V8JS_MN(define), v8::External::New(c));
	define->Set(V8JS_SYM("amd"), v8::ObjectTemplate::New());
	global->Set(V8JS_SYM("define"), define, v8::ReadOnly);
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
