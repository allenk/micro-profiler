//	Copyright (C) 2011 by Artem A. Gevorkyan (gevorkyan.org)
//
//	Permission is hereby granted, free of charge, to any person obtaining a copy
//	of this software and associated documentation files (the "Software"), to deal
//	in the Software without restriction, including without limitation the rights
//	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//	copies of the Software, and to permit persons to whom the Software is
//	furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//	THE SOFTWARE.

#include "symbol_resolver.h"

#include "../common/primitives.h"

#include <atlstr.h>
#include <dia2.h>
#include <psapi.h>
#include <utility>
#include <hash_map>

using namespace std;

namespace
{
	class dia_symbol_resolver : public symbol_resolver
	{
		typedef stdext::hash_map<const void *, wstring, micro_profiler::address_compare> names_cache;

		CComPtr<IDiaDataSource> _data_source;
		CComPtr<IDiaSession> _session;
		mutable names_cache _cached_names;

	public:
		dia_symbol_resolver(const wstring &image_path, unsigned __int64 load_address);
		virtual ~dia_symbol_resolver();

		virtual wstring symbol_name_by_va(const void *address) const;
	};


	dia_symbol_resolver::dia_symbol_resolver(const wstring &image_path, unsigned __int64 load_address)
	{
		_data_source.CoCreateInstance(CLSID_DiaSource);
		_data_source->loadDataForExe(CStringW(image_path.c_str()), NULL, NULL);
		_data_source->openSession(&_session);
		if (_session)
			_session->put_loadAddress(load_address);
	}

	dia_symbol_resolver::~dia_symbol_resolver()
	{	}

	wstring dia_symbol_resolver::symbol_name_by_va(const void *address) const
	{
		names_cache::const_iterator i = _cached_names.find(address);

		if (i == _cached_names.end())
		{
			CStringW result;
			CComBSTR name;
			CComPtr<IDiaSymbol> symbol;

			if (_session && SUCCEEDED(_session->findSymbolByVA((ULONGLONG)address, SymTagFunction, &symbol)) && symbol && SUCCEEDED(symbol->get_name(&name)))
				result = name;
			else
				result.Format(L"Function @%08I64X", (__int64)address);
			i = _cached_names.insert(make_pair(address, result)).first;
		}
		return i->second;
	}
}

shared_ptr<symbol_resolver> symbol_resolver::create_dia_resolver(const wstring &image_path, unsigned __int64 load_address)
{	return shared_ptr<symbol_resolver>(new dia_symbol_resolver(image_path, load_address));	}
