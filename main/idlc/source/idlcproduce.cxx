/**************************************************************
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * 
 *************************************************************/



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_idlc.hxx"
#include <idlc/idlc.hxx>
#include <idlc/astmodule.hxx>
#include <rtl/strbuf.hxx>
#include <osl/file.hxx>
#include <osl/thread.h>

#if defined(SAL_W32) || defined(SAL_OS2)
#include <io.h>
#include <direct.h>
#include <errno.h>
#endif

#ifdef SAL_UNX
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#endif

#include <string.h>

using namespace ::rtl;
using namespace ::osl;

StringList* pCreatedDirectories = NULL;

static sal_Bool checkOutputPath(const OString& completeName)
{
    OString sysPathName = convertToAbsoluteSystemPath(completeName);
	OStringBuffer buffer(sysPathName.getLength());

	if ( sysPathName.indexOf( SEPARATOR ) == -1 )
		return sal_True;

    sal_Int32 nIndex = 0;
	OString token(sysPathName.getToken(0, SEPARATOR, nIndex));
	const sal_Char* p = token.getStr();
	if (strcmp(p, "..") == 0
        || *(p+1) == ':'
        || strcmp(p, ".") == 0)
	{
		buffer.append(token);
		buffer.append(SEPARATOR);
	}
    else
        nIndex = 0;
		
    do
	{
		buffer.append(sysPathName.getToken(0, SEPARATOR, nIndex));

		if ( buffer.getLength() > 0 && nIndex != -1 )
		{
#if defined(SAL_UNX) || defined(SAL_OS2)
			if (mkdir((char*)buffer.getStr(), 0777) == -1)
#else
			if (mkdir((char*)buffer.getStr()) == -1)
#endif
			{
				if (errno == ENOENT)
				{	
					fprintf(stderr, "%s: cannot create directory '%s'\n", 
							idlc()->getOptions()->getProgramName().getStr(), buffer.getStr());
					return sal_False;
				}
	        } else
			{
				if ( !pCreatedDirectories )
					pCreatedDirectories = new StringList();
				pCreatedDirectories->push_front(buffer.getStr());				
			}
		}
        buffer.append(SEPARATOR);
	} while( nIndex != -1 );
	return sal_True;
}	

static sal_Bool cleanPath()
{
	if ( pCreatedDirectories )
	{
		StringList::iterator iter = pCreatedDirectories->begin();
		StringList::iterator end = pCreatedDirectories->end();
		while ( iter != end )
		{
//#ifdef SAL_UNX
//			if (rmdir((char*)(*iter).getStr(), 0777) == -1)
//#else
			if (rmdir((char*)(*iter).getStr()) == -1)
//#endif
			{
				fprintf(stderr, "%s: cannot remove directory '%s'\n", 
						idlc()->getOptions()->getProgramName().getStr(), (*iter).getStr());
				return sal_False;
			}
			++iter;
		}
		delete pCreatedDirectories;
	}
	return sal_True;
}

void removeIfExists(const OString& pathname)
{
    unlink(pathname.getStr());
}
	
sal_Int32 SAL_CALL produceFile(const OString& regFileName)
{
	Options* pOptions = idlc()->getOptions();

    OString regTmpName = regFileName.replaceAt(regFileName.getLength() -3, 3, "_idlc_");

	if ( !checkOutputPath(regFileName) )	
	{
		fprintf(stderr, "%s: could not create path of registry file '%s'.\n", 
				pOptions->getProgramName().getStr(), regFileName.getStr());
		return 1;
	}	

	removeIfExists(regTmpName);
    OString urlRegTmpName = convertToFileUrl(regTmpName);

	Registry regFile;
	if ( regFile.create(OStringToOUString(urlRegTmpName, RTL_TEXTENCODING_UTF8)) != REG_NO_ERROR )
	{
		fprintf(stderr, "%s: could not create registry file '%s'\n", 
				pOptions->getProgramName().getStr(), regTmpName.getStr());
		removeIfExists(regTmpName);
		removeIfExists(regFileName);
		cleanPath();
		return 1;	
	}

	RegistryKey rootKey;
	if ( regFile.openRootKey(rootKey) != REG_NO_ERROR )
	{
		fprintf(stderr, "%s: could not open root of registry file '%s'\n", 
				pOptions->getProgramName().getStr(), regFileName.getStr());
		removeIfExists(regTmpName);
		removeIfExists(regFileName);
		cleanPath();
		return 1;			
	}

	// produce registry file 
	if ( !idlc()->getRoot()->dump(rootKey) )
	{
		rootKey.releaseKey();
		regFile.close();
		regFile.destroy(OStringToOUString(regFileName, RTL_TEXTENCODING_UTF8));
		removeIfExists(regFileName);
		cleanPath();
		return 1;
	} 

	rootKey.releaseKey();
	if ( regFile.close() != REG_NO_ERROR )
	{
		fprintf(stderr, "%s: could not close registry file '%s'\n", 
				pOptions->getProgramName().getStr(), regFileName.getStr());
		removeIfExists(regTmpName);
		removeIfExists(regFileName);
		cleanPath();
		return 1;
	}

	removeIfExists(regFileName);

    if ( File::move(OStringToOUString(regTmpName, osl_getThreadTextEncoding()),
                    OStringToOUString(regFileName, osl_getThreadTextEncoding())) != FileBase::E_None ) {
		fprintf(stderr, "%s: cannot rename temporary registry '%s' to '%s'\n", 
				idlc()->getOptions()->getProgramName().getStr(), 
				regTmpName.getStr(), regFileName.getStr());
		removeIfExists(regTmpName);
		cleanPath();
		return 1;
    }
	removeIfExists(regTmpName);
    
	return 0;
}
