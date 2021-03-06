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
#include "precompiled_desktop.hxx"

#include "dp_component.hrc"
#include "dp_backend.h"
#include "dp_platform.hxx"
#include "dp_ucb.h"
#include "rtl/string.hxx"
#include "rtl/strbuf.hxx"
#include "rtl/ustrbuf.hxx"
#include "rtl/uri.hxx"
#include "cppuhelper/exc_hlp.hxx"
#include "ucbhelper/content.hxx"
#include "comphelper/anytostring.hxx"
#include "comphelper/servicedecl.hxx"
#include "comphelper/sequence.hxx"
#include "xmlscript/xml_helper.hxx"
#include "svl/inettype.hxx"
#include "com/sun/star/lang/WrappedTargetRuntimeException.hpp"
#include "com/sun/star/container/XNameContainer.hpp"
#include "com/sun/star/container/XHierarchicalNameAccess.hpp"
#include "com/sun/star/container/XSet.hpp"
#include "com/sun/star/registry/XSimpleRegistry.hpp"
#include "com/sun/star/registry/XImplementationRegistration.hpp"
#include "com/sun/star/loader/XImplementationLoader.hpp"
#include "com/sun/star/io/XInputStream.hpp"
#include "com/sun/star/ucb/NameClash.hpp"
#include "com/sun/star/util/XMacroExpander.hpp"
#include <list>
#include <hash_map>
#include <vector>
#include <memory>
#include <algorithm>
#include "dp_compbackenddb.hxx"

using namespace ::dp_misc;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::ucb;
using ::rtl::OUString;
namespace css = com::sun::star;

namespace dp_registry {
namespace backend {
namespace component {
namespace {

typedef ::std::list<OUString> t_stringlist;
typedef ::std::vector< ::std::pair<OUString, OUString> > t_stringpairvec;

#define IMPLEMENTATION_NAME  "com.sun.star.comp.deployment.component.PackageRegistryBackend"

/** return a vector of bootstrap variables which have been provided
    as command arguments.
*/
::std::vector<OUString> getCmdBootstrapVariables()
{
    ::std::vector<OUString> ret;
    sal_uInt32 count = osl_getCommandArgCount();
    for (sal_uInt32 i = 0; i < count; i++)
    {
        OUString arg;
        osl_getCommandArg(i, &arg.pData);
        if (arg.matchAsciiL("-env:", 5))
            ret.push_back(arg);
    }
    return ret; 
}

bool jarManifestHeaderPresent(
    OUString const & url, OUString const & name,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    ::rtl::OUStringBuffer buf;
    buf.appendAscii( RTL_CONSTASCII_STRINGPARAM("vnd.sun.star.zip://") );
    buf.append(
        ::rtl::Uri::encode(
            url, rtl_UriCharClassRegName, rtl_UriEncodeIgnoreEscapes,
            RTL_TEXTENCODING_UTF8 ) );
    buf.appendAscii( RTL_CONSTASCII_STRINGPARAM("/META-INF/MANIFEST.MF") );
    ::ucbhelper::Content manifestContent;
    OUString line;
    return
        create_ucb_content(
            &manifestContent, buf.makeStringAndClear(), xCmdEnv,
            false /* no throw */ )
        && readLine( &line, name, manifestContent, RTL_TEXTENCODING_ASCII_US );
}

//==============================================================================
class BackendImpl : public ::dp_registry::backend::PackageRegistryBackend
{
    class ComponentPackageImpl : public ::dp_registry::backend::Package
    {
        BackendImpl * getMyBackend() const;

        const OUString m_loader;

        enum reg {
            REG_UNINIT, REG_VOID, REG_REGISTERED, REG_NOT_REGISTERED, REG_MAYBE_REGISTERED
        } m_registered;
        
        void getComponentInfo(
            ComponentBackendDb::Data * data,
            std::vector< css::uno::Reference< css::uno::XInterface > > *
                factories,
            Reference<XComponentContext> const & xContext );
        
        virtual void SAL_CALL disposing();
        
        // Package
        virtual beans::Optional< beans::Ambiguous<sal_Bool> > isRegistered_(
            ::osl::ResettableMutexGuard & guard,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
        virtual void processPackage_(
            ::osl::ResettableMutexGuard & guard,
            bool registerPackage,
            bool startup,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
        
        const Reference<registry::XSimpleRegistry> getRDB() const;
        
        //Provides the read-only registry (e.g. not the one based on the duplicated
        //rdb files
        const Reference<registry::XSimpleRegistry> getRDB_RO() const;
        
    public:
        ComponentPackageImpl(
            ::rtl::Reference<PackageRegistryBackend> const & myBackend,
            OUString const & url, OUString const & name,
            Reference<deployment::XPackageTypeInfo> const & xPackageType,
            OUString const & loader, bool bRemoved,
            OUString const & identifier);
    };
    friend class ComponentPackageImpl;

    class ComponentsPackageImpl : public ::dp_registry::backend::Package
    {
        BackendImpl * getMyBackend() const;

        // Package
        virtual beans::Optional< beans::Ambiguous<sal_Bool> > isRegistered_(
            ::osl::ResettableMutexGuard & guard,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
        virtual void processPackage_(
            ::osl::ResettableMutexGuard & guard,
            bool registerPackage,
            bool startup,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
    public:
        ComponentsPackageImpl(
            ::rtl::Reference<PackageRegistryBackend> const & myBackend,
            OUString const & url, OUString const & name,
            Reference<deployment::XPackageTypeInfo> const & xPackageType,
            bool bRemoved, OUString const & identifier);
    };
    friend class ComponentsPackageImpl;

    class TypelibraryPackageImpl : public ::dp_registry::backend::Package
    {
        BackendImpl * getMyBackend() const;
        
        const bool m_jarFile;
        Reference<container::XHierarchicalNameAccess> m_xTDprov;
         
        virtual void SAL_CALL disposing();
        
        // Package
        virtual beans::Optional< beans::Ambiguous<sal_Bool> > isRegistered_(
            ::osl::ResettableMutexGuard & guard,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
        virtual void processPackage_(
            ::osl::ResettableMutexGuard & guard,
            bool registerPackage,
            bool startup,
            ::rtl::Reference<AbortChannel> const & abortChannel,
            Reference<XCommandEnvironment> const & xCmdEnv );
        
    public:
        TypelibraryPackageImpl(
            ::rtl::Reference<PackageRegistryBackend> const & myBackend,
            OUString const & url, OUString const & name,
            Reference<deployment::XPackageTypeInfo> const & xPackageType,
            bool jarFile, bool bRemoved,
            OUString const & identifier);
    };
    friend class TypelibraryPackageImpl;
    
    t_stringlist m_jar_typelibs;
    t_stringlist m_rdb_typelibs;
    t_stringlist m_components;

    enum RcItem { RCITEM_JAR_TYPELIB, RCITEM_RDB_TYPELIB, RCITEM_COMPONENTS };

    t_stringlist & getRcItemList( RcItem kind ) {
        switch (kind)
        {
        case RCITEM_JAR_TYPELIB:
            return m_jar_typelibs;
        case RCITEM_RDB_TYPELIB:
            return m_rdb_typelibs;
        default: // case RCITEM_COMPONENTS
            return m_components;
        }
    }
    
    bool m_unorc_inited;
    bool m_unorc_modified;
    bool bSwitchedRdbFiles;
    
    typedef ::std::hash_map< OUString, Reference<XInterface>,
                             ::rtl::OUStringHash > t_string2object;
    t_string2object m_backendObjects;
    
    // PackageRegistryBackend
    virtual Reference<deployment::XPackage> bindPackage_(
        OUString const & url, OUString const & mediaType,
        sal_Bool bRemoved, OUString const & identifier,
        Reference<XCommandEnvironment> const & xCmdEnv );
    
    virtual void SAL_CALL disposing();
    
    const Reference<deployment::XPackageTypeInfo> m_xDynComponentTypeInfo;
    const Reference<deployment::XPackageTypeInfo> m_xJavaComponentTypeInfo;
    const Reference<deployment::XPackageTypeInfo> m_xPythonComponentTypeInfo;
    const Reference<deployment::XPackageTypeInfo> m_xComponentsTypeInfo;
    const Reference<deployment::XPackageTypeInfo> m_xRDBTypelibTypeInfo;
    const Reference<deployment::XPackageTypeInfo> m_xJavaTypelibTypeInfo;
    Sequence< Reference<deployment::XPackageTypeInfo> > m_typeInfos;
    
    OUString m_commonRDB;
    OUString m_nativeRDB;

    //URLs of the read-only rdbs (e.g. not the ones of the duplicated files)
    OUString m_commonRDB_RO;
    OUString m_nativeRDB_RO;

    std::auto_ptr<ComponentBackendDb> m_backendDb;

    void addDataToDb(OUString const & url, ComponentBackendDb::Data const & data);
    ComponentBackendDb::Data readDataFromDb(OUString const & url);
    void revokeEntryFromDb(OUString const & url);


    //These rdbs are for writing new service entries. The rdb files are copies 
    //which are created when services are added or removed.
    Reference<registry::XSimpleRegistry> m_xCommonRDB;
    Reference<registry::XSimpleRegistry> m_xNativeRDB;

    //These rdbs are created on the read-only rdbs which are already used
    //by UNO since the startup of the current session. 
    Reference<registry::XSimpleRegistry> m_xCommonRDB_RO;
    Reference<registry::XSimpleRegistry> m_xNativeRDB_RO;

    
    void unorc_verify_init( Reference<XCommandEnvironment> const & xCmdEnv );
    void unorc_flush( Reference<XCommandEnvironment> const & xCmdEnv );
    
    Reference<XInterface> getObject( OUString const & id );
    Reference<XInterface> insertObject(
        OUString const & id, Reference<XInterface> const & xObject );
    void releaseObject( OUString const & id );
    
    bool addToUnoRc( RcItem kind, OUString const & url,
                     Reference<XCommandEnvironment> const & xCmdEnv );
    bool removeFromUnoRc( RcItem kind, OUString const & url,
                          Reference<XCommandEnvironment> const & xCmdEnv );
    bool hasInUnoRc( RcItem kind, OUString const & url );

    css::uno::Reference< css::registry::XRegistryKey > openRegistryKey(
        css::uno::Reference< css::registry::XRegistryKey > const & base,
        rtl::OUString const & path);

    void extractComponentData(
        css::uno::Reference< css::uno::XComponentContext > const & context,
        css::uno::Reference< css::registry::XRegistryKey > const & registry,
        ComponentBackendDb::Data * data,
        std::vector< css::uno::Reference< css::uno::XInterface > > * factories,
        css::uno::Reference< css::loader::XImplementationLoader > const *
            componentLoader,
        rtl::OUString const * componentUrl);

    void componentLiveInsertion(
        ComponentBackendDb::Data const & data,
        std::vector< css::uno::Reference< css::uno::XInterface > > const &
            factories);

    void componentLiveRemoval(ComponentBackendDb::Data const & data);

public:
    BackendImpl( Sequence<Any> const & args,
                 Reference<XComponentContext> const & xComponentContext );
    
    // XPackageRegistry
    virtual Sequence< Reference<deployment::XPackageTypeInfo> > SAL_CALL
    getSupportedPackageTypes() throw (RuntimeException);

    virtual void SAL_CALL packageRemoved(OUString const & url, OUString const & mediaType)
        throw (deployment::DeploymentException,
               uno::RuntimeException);

    using PackageRegistryBackend::disposing;

    //Will be called from ComponentPackageImpl
    void initServiceRdbFiles();

    //Creates the READ ONLY registries (m_xCommonRDB_RO,m_xNativeRDB_RO)
    void initServiceRdbFiles_RO();
};

//______________________________________________________________________________

BackendImpl::ComponentPackageImpl::ComponentPackageImpl(
    ::rtl::Reference<PackageRegistryBackend> const & myBackend,
    OUString const & url, OUString const & name,
    Reference<deployment::XPackageTypeInfo> const & xPackageType,
    OUString const & loader, bool bRemoved,
    OUString const & identifier)
    : Package( myBackend, url, name, name /* display-name */,
               xPackageType, bRemoved, identifier),
      m_loader( loader ),
      m_registered( REG_UNINIT )
{}

const Reference<registry::XSimpleRegistry>
BackendImpl::ComponentPackageImpl::getRDB() const
{
    BackendImpl * that = getMyBackend();

    //Late "initialization" of the services rdb files
    //This is to prevent problems when running several
    //instances of OOo with root rights in parallel. This
    //would otherwise cause problems when copying the rdbs.
    //See  http://qa.openoffice.org/issues/show_bug.cgi?id=99257
    {
        const ::osl::MutexGuard guard( getMutex() );
        if (!that->bSwitchedRdbFiles)
        {
            that->bSwitchedRdbFiles = true;
            that->initServiceRdbFiles();
        }
    }
    if (m_loader.equalsAsciiL(
            RTL_CONSTASCII_STRINGPARAM("com.sun.star.loader.SharedLibrary") ))
        return that->m_xNativeRDB;
    else
        return that->m_xCommonRDB;
}

//Returns the read only RDB.
const Reference<registry::XSimpleRegistry>
BackendImpl::ComponentPackageImpl::getRDB_RO() const
{
    BackendImpl * that = getMyBackend();

    if (m_loader.equalsAsciiL(
            RTL_CONSTASCII_STRINGPARAM("com.sun.star.loader.SharedLibrary") ))
        return that->m_xNativeRDB_RO;
    else
        return that->m_xCommonRDB_RO;
}

BackendImpl * BackendImpl::ComponentPackageImpl::getMyBackend() const
{
    BackendImpl * pBackend = static_cast<BackendImpl *>(m_myBackend.get());
    if (NULL == pBackend)
    {    
        //Throws a DisposedException
        check();
        //We should never get here...
        throw RuntimeException(
            OUSTR("Failed to get the BackendImpl"), 
            static_cast<OWeakObject*>(const_cast<ComponentPackageImpl *>(this)));
    }
    return pBackend;
}


//______________________________________________________________________________
void BackendImpl::ComponentPackageImpl::disposing()
{
//    m_xRemoteContext.clear();
    Package::disposing();
}

//______________________________________________________________________________
void BackendImpl::TypelibraryPackageImpl::disposing()
{
    m_xTDprov.clear();
    Package::disposing();
}

//______________________________________________________________________________
void BackendImpl::disposing()
{
    try {
        m_backendObjects = t_string2object();
        if (m_xNativeRDB.is()) {
            m_xNativeRDB->close();
            m_xNativeRDB.clear();
        }
        if (m_xCommonRDB.is()) {
            m_xCommonRDB->close();
            m_xCommonRDB.clear();
        }
        unorc_flush( Reference<XCommandEnvironment>() );
        
        PackageRegistryBackend::disposing();
    }
    catch (RuntimeException &) {
        throw;
    }
    catch (Exception &) {
        Any exc( ::cppu::getCaughtException() );
        throw lang::WrappedTargetRuntimeException(
            OUSTR("caught unexpected exception while disposing..."),
            static_cast<OWeakObject *>(this), exc );
    }
}


void BackendImpl::initServiceRdbFiles()
{
    const Reference<XCommandEnvironment> xCmdEnv;

    ::ucbhelper::Content cacheDir( getCachePath(), xCmdEnv );
    ::ucbhelper::Content oldRDB;
    // switch common rdb:
    if (m_commonRDB_RO.getLength() > 0)
    {
        create_ucb_content(
            &oldRDB, makeURL( getCachePath(), m_commonRDB_RO),
            xCmdEnv, false /* no throw */ );
    }
    m_commonRDB = m_commonRDB_RO.equalsAsciiL(
        RTL_CONSTASCII_STRINGPARAM("common.rdb") )
        ? OUSTR("common_.rdb") : OUSTR("common.rdb");
    if (oldRDB.get().is())
    {
        if (! cacheDir.transferContent(
                oldRDB, ::ucbhelper::InsertOperation_COPY,
                m_commonRDB, NameClash::OVERWRITE ))
        {

            throw RuntimeException(
                OUSTR("UCB transferContent() failed!"), 0 );
        }
        oldRDB = ::ucbhelper::Content();
    }
    // switch native rdb:
    if (m_nativeRDB_RO.getLength() > 0)
    {
        create_ucb_content(
            &oldRDB, makeURL(getCachePath(), m_nativeRDB_RO),
            xCmdEnv, false /* no throw */ );
    }
    const OUString plt_rdb( getPlatformString() + OUSTR(".rdb") );
    const OUString plt_rdb_( getPlatformString() + OUSTR("_.rdb") );
    m_nativeRDB = m_nativeRDB_RO.equals( plt_rdb ) ? plt_rdb_ : plt_rdb;
    if (oldRDB.get().is())
    {
        if (! cacheDir.transferContent(
                oldRDB, ::ucbhelper::InsertOperation_COPY,
                m_nativeRDB, NameClash::OVERWRITE ))
            throw RuntimeException(
                OUSTR("UCB transferContent() failed!"), 0 );
    }

    // UNO is bootstrapped, flush for next process start:
    m_unorc_modified = true;
    unorc_flush( Reference<XCommandEnvironment>() );


    // common rdb for java, native rdb for shared lib components
    if (m_commonRDB.getLength() > 0) {
        m_xCommonRDB.set(
            m_xComponentContext->getServiceManager()
            ->createInstanceWithContext(
            OUSTR("com.sun.star.registry.SimpleRegistry"),
            m_xComponentContext ), UNO_QUERY_THROW );
        m_xCommonRDB->open(
            makeURL( expandUnoRcUrl(getCachePath()), m_commonRDB ),
//            m_readOnly, !m_readOnly );
            false, true);
    }
    if (m_nativeRDB.getLength() > 0) {
        m_xNativeRDB.set(
            m_xComponentContext->getServiceManager()
            ->createInstanceWithContext(
            OUSTR("com.sun.star.registry.SimpleRegistry"),
            m_xComponentContext ), UNO_QUERY_THROW );
        m_xNativeRDB->open(
            makeURL( expandUnoRcUrl(getCachePath()), m_nativeRDB ),
//            m_readOnly, !m_readOnly );
            false, true);
    }
}

void BackendImpl::initServiceRdbFiles_RO()
{
    const Reference<XCommandEnvironment> xCmdEnv;

    // common rdb for java, native rdb for shared lib components
    if (m_commonRDB_RO.getLength() > 0) 
    {
        m_xCommonRDB_RO.set(
            m_xComponentContext->getServiceManager()
            ->createInstanceWithContext(
            OUSTR("com.sun.star.registry.SimpleRegistry"),
            m_xComponentContext), UNO_QUERY_THROW);
        m_xCommonRDB_RO->open(
            makeURL(expandUnoRcUrl(getCachePath()), m_commonRDB_RO),
            sal_True, //read-only 
            sal_True); // create data source if necessary
    }
    if (m_nativeRDB_RO.getLength() > 0) 
    {
        m_xNativeRDB_RO.set(
            m_xComponentContext->getServiceManager()
            ->createInstanceWithContext(
            OUSTR("com.sun.star.registry.SimpleRegistry"),
            m_xComponentContext), UNO_QUERY_THROW);
        m_xNativeRDB_RO->open(
            makeURL(expandUnoRcUrl(getCachePath()), m_nativeRDB_RO),
            sal_True, //read-only 
            sal_True); // create data source if necessary
    }
}

//______________________________________________________________________________
BackendImpl::BackendImpl(
    Sequence<Any> const & args,
    Reference<XComponentContext> const & xComponentContext )
    : PackageRegistryBackend( args, xComponentContext ),
      m_unorc_inited( false ),
      m_unorc_modified( false ),
      bSwitchedRdbFiles(false),
      m_xDynComponentTypeInfo( new Package::TypeInfo(
                                   OUSTR("application/"
                                         "vnd.sun.star.uno-component;"
                                         "type=native;platform=") +
                                   getPlatformString(),
                                   OUSTR("*" SAL_DLLEXTENSION),
                                   getResourceString(RID_STR_DYN_COMPONENT),
                                   RID_IMG_COMPONENT,
                                   RID_IMG_COMPONENT_HC ) ),
      m_xJavaComponentTypeInfo( new Package::TypeInfo(
                                    OUSTR("application/"
                                          "vnd.sun.star.uno-component;"
                                          "type=Java"),
                                    OUSTR("*.jar"),
                                    getResourceString(RID_STR_JAVA_COMPONENT),
                                    RID_IMG_JAVA_COMPONENT,
                                    RID_IMG_JAVA_COMPONENT_HC ) ),
      m_xPythonComponentTypeInfo( new Package::TypeInfo(
                                      OUSTR("application/"
                                            "vnd.sun.star.uno-component;"
                                            "type=Python"),
                                      OUSTR("*.py"),
                                      getResourceString(
                                          RID_STR_PYTHON_COMPONENT),
                                      RID_IMG_COMPONENT,
                                      RID_IMG_COMPONENT_HC ) ),
      m_xComponentsTypeInfo( new Package::TypeInfo(
                                 OUSTR("application/"
                                       "vnd.sun.star.uno-components"),
                                 OUSTR("*.components"),
                                 getResourceString(RID_STR_COMPONENTS),
                                 RID_IMG_COMPONENT,
                                 RID_IMG_COMPONENT_HC ) ),
      m_xRDBTypelibTypeInfo( new Package::TypeInfo(
                                 OUSTR("application/"
                                       "vnd.sun.star.uno-typelibrary;"
                                       "type=RDB"),
                                 OUSTR("*.rdb"),
                                 getResourceString(RID_STR_RDB_TYPELIB),
                                 RID_IMG_TYPELIB, RID_IMG_TYPELIB_HC ) ),
      m_xJavaTypelibTypeInfo( new Package::TypeInfo(
                                  OUSTR("application/"
                                        "vnd.sun.star.uno-typelibrary;"
                                        "type=Java"),
                                  OUSTR("*.jar"),
                                  getResourceString(RID_STR_JAVA_TYPELIB),
                                  RID_IMG_JAVA_TYPELIB,
                                  RID_IMG_JAVA_TYPELIB_HC ) ),
      m_typeInfos( 6 )
{
    m_typeInfos[ 0 ] = m_xDynComponentTypeInfo;
    m_typeInfos[ 1 ] = m_xJavaComponentTypeInfo;
    m_typeInfos[ 2 ] = m_xPythonComponentTypeInfo;
    m_typeInfos[ 3 ] = m_xComponentsTypeInfo;
    m_typeInfos[ 4 ] = m_xRDBTypelibTypeInfo;
    m_typeInfos[ 5 ] = m_xJavaTypelibTypeInfo;
    
    const Reference<XCommandEnvironment> xCmdEnv;
    
    if (transientMode())
    {
        // in-mem rdbs:
        // common rdb for java, native rdb for shared lib components
        m_xCommonRDB.set(
            xComponentContext->getServiceManager()->createInstanceWithContext(
                OUSTR("com.sun.star.registry.SimpleRegistry"),
                xComponentContext ), UNO_QUERY_THROW );
        m_xCommonRDB->open( OUString() /* in-mem */,
                            false /* ! read-only */, true /* create */ );
        m_xNativeRDB.set(
            xComponentContext->getServiceManager()->createInstanceWithContext(
                OUSTR("com.sun.star.registry.SimpleRegistry"),
                xComponentContext ), UNO_QUERY_THROW );
        m_xNativeRDB->open( OUString() /* in-mem */,
                            false /* ! read-only */, true /* create */ );
    }
    else
    {
        //do this before initServiceRdbFiles_RO, because it determines
        //m_commonRDB and m_nativeRDB
        unorc_verify_init( xCmdEnv );

        initServiceRdbFiles_RO();

        OUString dbFile = makeURL(getCachePath(), OUSTR("backenddb.xml"));
        m_backendDb.reset(
            new ComponentBackendDb(getComponentContext(), dbFile));
    }
}

void BackendImpl::addDataToDb(
    OUString const & url, ComponentBackendDb::Data const & data)
{
    if (m_backendDb.get())
        m_backendDb->addEntry(url, data);
}

ComponentBackendDb::Data BackendImpl::readDataFromDb(OUString const & url)
{
    ComponentBackendDb::Data data;
    if (m_backendDb.get())
        data = m_backendDb->getEntry(url);
    return data;
}

void BackendImpl::revokeEntryFromDb(OUString const & url)
{
    if (m_backendDb.get())
        m_backendDb->revokeEntry(url);
}

// XPackageRegistry
//______________________________________________________________________________
Sequence< Reference<deployment::XPackageTypeInfo> >
BackendImpl::getSupportedPackageTypes() throw (RuntimeException)
{
    return m_typeInfos;
}

void BackendImpl::packageRemoved(OUString const & url, OUString const & /*mediaType*/)
        throw (deployment::DeploymentException,
               uno::RuntimeException)
{
    if (m_backendDb.get())
        m_backendDb->removeEntry(url);
}

// PackageRegistryBackend
//______________________________________________________________________________
Reference<deployment::XPackage> BackendImpl::bindPackage_(
    OUString const & url, OUString const & mediaType_,
    sal_Bool bRemoved, OUString const & identifier,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    OUString mediaType(mediaType_);
    if (mediaType.getLength() == 0 ||
        mediaType.equalsAsciiL(
            RTL_CONSTASCII_STRINGPARAM(
                "application/vnd.sun.star.uno-component") ) ||
        mediaType.equalsAsciiL(
            RTL_CONSTASCII_STRINGPARAM(
                "application/vnd.sun.star.uno-typelibrary") ))
    {
        // detect exact media-type:
        ::ucbhelper::Content ucbContent;
        if (create_ucb_content( &ucbContent, url, xCmdEnv )) {
            const OUString title( ucbContent.getPropertyValue(
                                      StrTitle::get() ).get<OUString>() );
            if (title.endsWithIgnoreAsciiCaseAsciiL(
                    RTL_CONSTASCII_STRINGPARAM(SAL_DLLEXTENSION) ))
            {
                mediaType = OUSTR("application/vnd.sun.star.uno-component;"
                                  "type=native;platform=") +
                    getPlatformString();
            }
            else if (title.endsWithIgnoreAsciiCaseAsciiL(
                         RTL_CONSTASCII_STRINGPARAM(".jar") ))
            {
                if (jarManifestHeaderPresent(
                        url, OUSTR("RegistrationClassName"), xCmdEnv ))
                    mediaType = OUSTR(
                        "application/vnd.sun.star.uno-component;type=Java");
                if (mediaType.getLength() == 0)
                    mediaType = OUSTR(
                        "application/vnd.sun.star.uno-typelibrary;type=Java");
            }
            else if (title.endsWithIgnoreAsciiCaseAsciiL(
                         RTL_CONSTASCII_STRINGPARAM(".py") ))
                mediaType =
                    OUSTR("application/vnd.sun.star.uno-component;type=Python");
            else if (title.endsWithIgnoreAsciiCaseAsciiL(
                         RTL_CONSTASCII_STRINGPARAM(".rdb") ))
                mediaType =
                    OUSTR("application/vnd.sun.star.uno-typelibrary;type=RDB");
        }
        if (mediaType.getLength() == 0)
            throw lang::IllegalArgumentException(
                StrCannotDetectMediaType::get() + url,
                static_cast<OWeakObject *>(this), static_cast<sal_Int16>(-1) );
    }
    
    String type, subType;
    INetContentTypeParameterList params;
    if (INetContentTypes::parse( mediaType, type, subType, &params ))
    {
        if (type.EqualsIgnoreCaseAscii("application"))
        {
            OUString name;
            if (!bRemoved)
            {
                ::ucbhelper::Content ucbContent( url, xCmdEnv );
                name = ucbContent.getPropertyValue(
                    StrTitle::get() ).get<OUString>();
            }
            
            if (subType.EqualsIgnoreCaseAscii("vnd.sun.star.uno-component"))
            {
                // xxx todo: probe and evaluate component xml description
                
                INetContentTypeParameter const * param = params.find(
                    ByteString("platform") );
                if (param == 0 || platform_fits( param->m_sValue )) {
                    param = params.find( ByteString("type") );
                    if (param != 0)
                    {
                        String const & value = param->m_sValue;
                        if (value.EqualsIgnoreCaseAscii("native")) {
                            return new BackendImpl::ComponentPackageImpl(
                                this, url, name, m_xDynComponentTypeInfo,
                                OUSTR("com.sun.star.loader.SharedLibrary"),
                                bRemoved, identifier);
                        }
                        if (value.EqualsIgnoreCaseAscii("Java")) {
                            return new BackendImpl::ComponentPackageImpl(
                                this, url, name, m_xJavaComponentTypeInfo,
                                OUSTR("com.sun.star.loader.Java2"),
                                bRemoved, identifier);
                        }
                        if (value.EqualsIgnoreCaseAscii("Python")) {
                            return new BackendImpl::ComponentPackageImpl(
                                this, url, name, m_xPythonComponentTypeInfo,
                                OUSTR("com.sun.star.loader.Python"),
                                bRemoved, identifier);
                        }
                    }
                }
            }
            else if (subType.EqualsIgnoreCaseAscii(
                         "vnd.sun.star.uno-components"))
            {
                INetContentTypeParameter const * param = params.find(
                    ByteString("platform") );
                if (param == 0 || platform_fits( param->m_sValue )) {
                    return new BackendImpl::ComponentsPackageImpl(
                        this, url, name, m_xComponentsTypeInfo, bRemoved,
                        identifier);
                }
            }
            else if (subType.EqualsIgnoreCaseAscii(
                         "vnd.sun.star.uno-typelibrary"))
            {
                INetContentTypeParameter const * param = params.find(
                    ByteString("type") );
                if (param != 0) {
                    String const & value = param->m_sValue;
                    if (value.EqualsIgnoreCaseAscii("RDB"))
                    {
                        return new BackendImpl::TypelibraryPackageImpl(
                            this, url, name, m_xRDBTypelibTypeInfo,
                            false /* rdb */, bRemoved, identifier);
                    }
                    if (value.EqualsIgnoreCaseAscii("Java")) {
                        return new BackendImpl::TypelibraryPackageImpl(
                            this, url, name, m_xJavaTypelibTypeInfo,
                            true /* jar */, bRemoved, identifier);
                    }
                }
            }
        }
    }
    throw lang::IllegalArgumentException(
        StrUnsupportedMediaType::get() + mediaType,
        static_cast<OWeakObject *>(this),
        static_cast<sal_Int16>(-1) );
}

//##############################################################################

//______________________________________________________________________________
void BackendImpl::unorc_verify_init(
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    if (transientMode())
        return;
    const ::osl::MutexGuard guard( getMutex() );
    if (! m_unorc_inited)
    {
        // common rc:
        ::ucbhelper::Content ucb_content;
        if (create_ucb_content(
                &ucb_content,
                makeURL( getCachePath(), OUSTR("unorc") ),
                xCmdEnv, false /* no throw */ ))
        {
            OUString line;
            if (readLine( &line, OUSTR("UNO_JAVA_CLASSPATH="), ucb_content,
                          RTL_TEXTENCODING_UTF8 ))
            {
                sal_Int32 index = sizeof ("UNO_JAVA_CLASSPATH=") - 1;
                do {
                    OUString token( line.getToken( 0, ' ', index ).trim() );
                    if (token.getLength() > 0) 
                    {
                        if (create_ucb_content(
                                0, expandUnoRcTerm(token), xCmdEnv,
                                false /* no throw */ ))
                        {
                            //The jar file may not exist anymore if a shared or bundled
                            //extension was removed, but it can still be in the unorc
                            //After running XExtensionManager::synchronize, the unorc is
                            //cleaned up
                            m_jar_typelibs.push_back( token );
                        }
                    }
                }
                while (index >= 0);
            }
            if (readLine( &line, OUSTR("UNO_TYPES="), ucb_content,
                          RTL_TEXTENCODING_UTF8 )) {
                sal_Int32 index = sizeof ("UNO_TYPES=") - 1;
                do {
                    OUString token( line.getToken( 0, ' ', index ).trim() );
                    if (token.getLength() > 0) 
                    {
                        if (token[ 0 ] == '?')
                            token = token.copy( 1 );
                         if (create_ucb_content(
                                0, expandUnoRcTerm(token), xCmdEnv,
                                false /* no throw */ ))
                         {
                             //The RDB file may not exist anymore if a shared or bundled
                             //extension was removed, but it can still be in the unorc.
                             //After running XExtensionManager::synchronize, the unorc is
                             //cleaned up
                             m_rdb_typelibs.push_back( token );
                         }
                    }
                }
                while (index >= 0);
            }
            if (readLine( &line, OUSTR("UNO_SERVICES="), ucb_content,
                          RTL_TEXTENCODING_UTF8 ))
            {
                // The UNO_SERVICES line always has the BNF form
                //  "UNO_SERVICES="
                //  ("?$ORIGIN/" <common-rdb>)?                        -- first
                //  "${$ORIGIN/${_OS}_${_ARCH}rc:UNO_SERVICES}"?       -- second
                //  ("?" ("BUNDLED_EXTENSIONS" |                       -- third
                //   "UNO_SHARED_PACKAGES_CACHE" | "UNO_USER_PACKAGES_CACHE")
                //   ...)*
                // so can unambiguously be split into its thre parts:
                int state = 1;
                for (sal_Int32 i = RTL_CONSTASCII_LENGTH("UNO_SERVICES=");
                     i >= 0;)
                {
                    rtl::OUString token(line.getToken(0, ' ', i));
                    if (token.getLength() != 0)
                    {
                        if (state == 1 &&
                            token.matchAsciiL(
                                RTL_CONSTASCII_STRINGPARAM("?$ORIGIN/")))
                        {
                            m_commonRDB_RO = token.copy(
                                RTL_CONSTASCII_LENGTH("?$ORIGIN/"));
                            state = 2;
                        }
                        else if (state <= 2 &&
                                 token.equalsAsciiL(
                                     RTL_CONSTASCII_STRINGPARAM(
                                         "${$ORIGIN/${_OS}_${_ARCH}rc:"
                                         "UNO_SERVICES}")))
                        {
                            state = 3;
                        }
                        else
                        {
                            if (token[0] == '?')
                            {
                                token = token.copy(1);
                            }
                            m_components.push_back(token);
                            state = 3;
                        }
                    }
                }
            }
            
            // native rc:
            if (create_ucb_content(
                    &ucb_content,
                    makeURL( getCachePath(), getPlatformString() + OUSTR("rc")),
                    xCmdEnv, false /* no throw */ )) {
                if (readLine( &line, OUSTR("UNO_SERVICES="), ucb_content,
                              RTL_TEXTENCODING_UTF8 )) {
                    m_nativeRDB_RO = line.copy(
                        sizeof ("UNO_SERVICES=?$ORIGIN/") - 1 );
                }
            }
        }
        m_unorc_modified = false;
        m_unorc_inited = true;
    }
}

//______________________________________________________________________________
void BackendImpl::unorc_flush( Reference<XCommandEnvironment> const & xCmdEnv )
{
    if (transientMode())
        return;
    if (!m_unorc_inited || !m_unorc_modified)
        return;
    
    ::rtl::OStringBuffer buf;

    buf.append(RTL_CONSTASCII_STRINGPARAM("ORIGIN="));
    OUString sOrigin = dp_misc::makeRcTerm(m_cachePath);
    ::rtl::OString osOrigin = ::rtl::OUStringToOString(sOrigin, RTL_TEXTENCODING_UTF8);
    buf.append(osOrigin);
    buf.append(LF);
    
    if (! m_jar_typelibs.empty())
    {
        t_stringlist::const_iterator iPos( m_jar_typelibs.begin() );
        t_stringlist::const_iterator const iEnd( m_jar_typelibs.end() );
        buf.append( RTL_CONSTASCII_STRINGPARAM("UNO_JAVA_CLASSPATH=") );
        while (iPos != iEnd) {
            // encoded ASCII file-urls:
            const ::rtl::OString item(
                ::rtl::OUStringToOString( *iPos, RTL_TEXTENCODING_ASCII_US ) );
            buf.append( item );
            ++iPos;
            if (iPos != iEnd)
                buf.append( ' ' );
        }
        buf.append(LF);
    }
    if (! m_rdb_typelibs.empty())
    {
        t_stringlist::const_iterator iPos( m_rdb_typelibs.begin() );
        t_stringlist::const_iterator const iEnd( m_rdb_typelibs.end() );
        buf.append( RTL_CONSTASCII_STRINGPARAM("UNO_TYPES=") );
        while (iPos != iEnd) {
            buf.append( '?' );
            // encoded ASCII file-urls:
            const ::rtl::OString item(
                ::rtl::OUStringToOString( *iPos, RTL_TEXTENCODING_ASCII_US ) );
            buf.append( item );
            ++iPos;
            if (iPos != iEnd)
                buf.append( ' ' );
        }
        buf.append(LF);
    }

    // If we duplicated the common or native rdb then we must use those urls
    //otherwise we use those of the original files. That is, m_commonRDB_RO and 
    //m_nativeRDB_RO;
    OUString sCommonRDB(m_commonRDB.getLength() > 0 ? m_commonRDB : m_commonRDB_RO);
    OUString sNativeRDB(m_nativeRDB.getLength() > 0 ? m_nativeRDB : m_nativeRDB_RO);

    if (sCommonRDB.getLength() > 0 || sNativeRDB.getLength() > 0 ||
        !m_components.empty())
    {
        buf.append( RTL_CONSTASCII_STRINGPARAM("UNO_SERVICES=") );
        bool space = false;
        if (sCommonRDB.getLength() > 0)
        {
            buf.append( RTL_CONSTASCII_STRINGPARAM("?$ORIGIN/") );
            buf.append( ::rtl::OUStringToOString(
                            sCommonRDB, RTL_TEXTENCODING_ASCII_US ) );
            space = true;
        }
        if (sNativeRDB.getLength() > 0)
        {
            if (space)
            {
                buf.append(' ');
            }
            buf.append( RTL_CONSTASCII_STRINGPARAM(
                            "${$ORIGIN/${_OS}_${_ARCH}rc:UNO_SERVICES}") );
            space = true;
            
            // write native rc:
            ::rtl::OStringBuffer buf2;
            buf2.append(RTL_CONSTASCII_STRINGPARAM("ORIGIN="));
            buf2.append(osOrigin);
            buf2.append(LF);
            buf2.append( RTL_CONSTASCII_STRINGPARAM("UNO_SERVICES=?$ORIGIN/") );
            buf2.append( ::rtl::OUStringToOString(
                             sNativeRDB, RTL_TEXTENCODING_ASCII_US ) );
            buf2.append(LF);
            
            const Reference<io::XInputStream> xData(
                ::xmlscript::createInputStream(
                    ::rtl::ByteSequence(
                        reinterpret_cast<sal_Int8 const *>(buf2.getStr()),
                        buf2.getLength() ) ) );
            ::ucbhelper::Content ucb_content(
                makeURL( getCachePath(), getPlatformString() + OUSTR("rc") ),
                xCmdEnv );
            ucb_content.writeStream( xData, true /* replace existing */ );
        }
        for (t_stringlist::iterator i(m_components.begin());
             i != m_components.end(); ++i)
        {
            if (space)
            {
                buf.append(' ');
            }
            buf.append('?');
            buf.append(rtl::OUStringToOString(*i, RTL_TEXTENCODING_UTF8));
            space = true;
        }
        buf.append(LF);
    }
    
    // write unorc:
    const Reference<io::XInputStream> xData(
        ::xmlscript::createInputStream(
            ::rtl::ByteSequence(
                reinterpret_cast<sal_Int8 const *>(buf.getStr()),
                buf.getLength() ) ) );
    ::ucbhelper::Content ucb_content(
        makeURL( getCachePath(), OUSTR("unorc") ), xCmdEnv );
    ucb_content.writeStream( xData, true /* replace existing */ );
    
    m_unorc_modified = false;
}

//______________________________________________________________________________
bool BackendImpl::addToUnoRc( RcItem kind, OUString const & url_,
                              Reference<XCommandEnvironment> const & xCmdEnv )
{
    const OUString rcterm( dp_misc::makeRcTerm(url_) );
    const ::osl::MutexGuard guard( getMutex() );
    unorc_verify_init( xCmdEnv );
    t_stringlist & rSet = getRcItemList(kind);
    if (::std::find( rSet.begin(), rSet.end(), rcterm ) == rSet.end()) {
        rSet.push_front( rcterm ); // prepend to list, thus overriding
        // write immediately:
        m_unorc_modified = true;
        unorc_flush( xCmdEnv );
        return true;
    }
    else
        return false;
}

//______________________________________________________________________________
bool BackendImpl::removeFromUnoRc(
    RcItem kind, OUString const & url_,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    const OUString rcterm( dp_misc::makeRcTerm(url_) );
    const ::osl::MutexGuard guard( getMutex() );
    unorc_verify_init( xCmdEnv );
    getRcItemList(kind).remove( rcterm );
    // write immediately:
    m_unorc_modified = true;
    unorc_flush( xCmdEnv );
    return true;
}

//______________________________________________________________________________
bool BackendImpl::hasInUnoRc(
    RcItem kind, OUString const & url_ )
{   
    const OUString rcterm( dp_misc::makeRcTerm(url_) );
    const ::osl::MutexGuard guard( getMutex() );
    t_stringlist const & rSet = getRcItemList(kind);
    return ::std::find( rSet.begin(), rSet.end(), rcterm ) != rSet.end();
}

css::uno::Reference< css::registry::XRegistryKey > BackendImpl::openRegistryKey(
    css::uno::Reference< css::registry::XRegistryKey > const & base,
    rtl::OUString const & path)
{
    OSL_ASSERT(base.is());
    css::uno::Reference< css::registry::XRegistryKey > key(base->openKey(path));
    if (!key.is()) {
        throw css::deployment::DeploymentException(
            (rtl::OUString(
                RTL_CONSTASCII_USTRINGPARAM("missing registry entry ")) +
             path + rtl::OUString(RTL_CONSTASCII_USTRINGPARAM(" under ")) +
             base->getKeyName()),
            static_cast< OWeakObject * >(this), Any());
    }
    return key;
}

void BackendImpl::extractComponentData(
    css::uno::Reference< css::uno::XComponentContext > const & context,
    css::uno::Reference< css::registry::XRegistryKey > const & registry,
    ComponentBackendDb::Data * data,
    std::vector< css::uno::Reference< css::uno::XInterface > > * factories,
    css::uno::Reference< css::loader::XImplementationLoader > const *
        componentLoader,
    rtl::OUString const * componentUrl)
{
    OSL_ASSERT(context.is() && registry.is() && data != 0 && factories != 0);
    rtl::OUString registryName(registry->getKeyName());
    sal_Int32 prefix = registryName.getLength();
    if (!registryName.endsWithAsciiL(RTL_CONSTASCII_STRINGPARAM("/"))) {
        prefix += RTL_CONSTASCII_LENGTH("/");
    }
    css::uno::Sequence< css::uno::Reference< css::registry::XRegistryKey > >
        keys(registry->openKeys());
    css::uno::Reference< css::lang::XMultiComponentFactory > smgr(
        context->getServiceManager(), css::uno::UNO_QUERY_THROW);
    for (sal_Int32 i = 0; i < keys.getLength(); ++i) {
        rtl::OUString name(keys[i]->getKeyName().copy(prefix));
        data->implementationNames.push_back(name);
        css::uno::Reference< css::registry::XRegistryKey > singletons(
            keys[i]->openKey(
                rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("UNO/SINGLETONS"))));
        if (singletons.is()) {
            sal_Int32 prefix2 = keys[i]->getKeyName().getLength() +
                RTL_CONSTASCII_LENGTH("/UNO/SINGLETONS/");
            css::uno::Sequence<
                css::uno::Reference< css::registry::XRegistryKey > >
                singletonKeys(singletons->openKeys());
            for (sal_Int32 j = 0; j < singletonKeys.getLength(); ++j) {
                data->singletons.push_back(
                    std::pair< rtl::OUString, rtl::OUString >(
                        singletonKeys[j]->getKeyName().copy(prefix2), name));
            }
        }
        css::uno::Reference< css::loader::XImplementationLoader > loader;
        if (componentLoader == 0) {
            rtl::OUString activator(
                openRegistryKey(
                    keys[i],
                    rtl::OUString(
                        RTL_CONSTASCII_USTRINGPARAM("UNO/ACTIVATOR")))->
                getAsciiValue());
            loader.set(
                smgr->createInstanceWithContext(activator, context),
                css::uno::UNO_QUERY);
            if (!loader.is()) {
                throw css::deployment::DeploymentException(
                    (rtl::OUString(
                        RTL_CONSTASCII_USTRINGPARAM(
                            "cannot instantiate loader ")) +
                     activator),
                    static_cast< OWeakObject * >(this), Any());
            }
        } else {
            OSL_ASSERT(componentLoader->is());
            loader = *componentLoader;
        }
        factories->push_back(
            loader->activate(
                name, rtl::OUString(),
                (componentUrl == 0
                 ? (openRegistryKey(
                        keys[i],
                        rtl::OUString(
                            RTL_CONSTASCII_USTRINGPARAM("UNO/LOCATION")))->
                    getAsciiValue())
                 : *componentUrl),
                keys[i]));
    }
}

void BackendImpl::componentLiveInsertion(
    ComponentBackendDb::Data const & data,
    std::vector< css::uno::Reference< css::uno::XInterface > > const &
        factories)
{
    css::uno::Reference< css::container::XSet > set(
        getComponentContext()->getServiceManager(), css::uno::UNO_QUERY_THROW);
    std::vector< css::uno::Reference< css::uno::XInterface > >::const_iterator
        factory(factories.begin());
    for (t_stringlist::const_iterator i(data.implementationNames.begin());
         i != data.implementationNames.end(); ++i)
    {
        try {
            set->insert(css::uno::Any(*factory++));
        } catch (container::ElementExistException &) {
            OSL_TRACE(
                "implementation %s already registered",
                rtl::OUStringToOString(*i, RTL_TEXTENCODING_UTF8).getStr());
        }
    }
    if (!data.singletons.empty()) {
        css::uno::Reference< css::container::XNameContainer >
            rootContext(
                getComponentContext()->getValueByName(
                    rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("_root"))),
                css::uno::UNO_QUERY);
        if (rootContext.is()) {
            for (t_stringpairvec::const_iterator i(data.singletons.begin());
                 i != data.singletons.end(); ++i)
            {
                rtl::OUString name(
                    rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("/singletons/")) +
                    i->first);
                try {
                    rootContext->removeByName(
                        name +
                        rtl::OUString(
                            RTL_CONSTASCII_USTRINGPARAM("/arguments")));
                } catch (container::NoSuchElementException &) {}
                try {
                    rootContext->insertByName(
                        (name +
                         rtl::OUString(
                             RTL_CONSTASCII_USTRINGPARAM("/service"))),
                        css::uno::Any(i->second));
                } catch (container::ElementExistException &) {
                    rootContext->replaceByName(
                        (name +
                         rtl::OUString(
                             RTL_CONSTASCII_USTRINGPARAM("/service"))),
                        css::uno::Any(i->second));
                }
                try {
                    rootContext->insertByName(name, css::uno::Any());
                } catch (container::ElementExistException &) {
                    OSL_TRACE(
                        "singleton %s already registered",
                        rtl::OUStringToOString(
                            i->first, RTL_TEXTENCODING_UTF8).getStr());
                    rootContext->replaceByName(name, css::uno::Any());
                }
            }
        }
    }
}

void BackendImpl::componentLiveRemoval(ComponentBackendDb::Data const & data) {
    css::uno::Reference< css::container::XSet > set(
        getComponentContext()->getServiceManager(), css::uno::UNO_QUERY_THROW);
    for (t_stringlist::const_iterator i(data.implementationNames.begin());
         i != data.implementationNames.end(); ++i)
    {
        try {
            set->remove(css::uno::Any(*i));
        } catch (css::container::NoSuchElementException &) {
            // ignore if factory has not been live deployed
        }
    }
    if (!data.singletons.empty()) {
        css::uno::Reference< css::container::XNameContainer > rootContext(
            getComponentContext()->getValueByName(
                rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("_root"))),
            css::uno::UNO_QUERY);
        if (rootContext.is()) {
            for (t_stringpairvec::const_iterator i(data.singletons.begin());
                 i != data.singletons.end(); ++i)
            {
                rtl::OUString name(
                    rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("/singletons/")) +
                    i->first);
                try {
                    rootContext->removeByName(
                        name +
                        rtl::OUString(
                            RTL_CONSTASCII_USTRINGPARAM("/arguments")));
                    rootContext->removeByName(
                        name +
                        rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("/service")));
                    rootContext->removeByName(name);
                } catch (container::NoSuchElementException &) {}
            }
        }
    }
}

//______________________________________________________________________________
void BackendImpl::releaseObject( OUString const & id )
{
    const ::osl::MutexGuard guard( getMutex() );
    m_backendObjects.erase( id );
}

//______________________________________________________________________________
Reference<XInterface> BackendImpl::getObject( OUString const & id )
{
    const ::osl::MutexGuard guard( getMutex() );
    const t_string2object::const_iterator iFind( m_backendObjects.find( id ) );
    if (iFind == m_backendObjects.end())
        return Reference<XInterface>();
    else
        return iFind->second;
}

//______________________________________________________________________________
Reference<XInterface> BackendImpl::insertObject(
    OUString const & id, Reference<XInterface> const & xObject )
{
    const ::osl::MutexGuard guard( getMutex() );
    const ::std::pair<t_string2object::iterator, bool> insertion(
        m_backendObjects.insert( t_string2object::value_type(
                                     id, xObject ) ) );
    return insertion.first->second;
}

//------------------------------------------------------------------------------
Reference<XComponentContext> raise_uno_process(
    Reference<XComponentContext> const & xContext,
    ::rtl::Reference<AbortChannel> const & abortChannel )
{
    OSL_ASSERT( xContext.is() );

    ::rtl::OUString url(
        Reference<util::XMacroExpander>(
            xContext->getValueByName(
                OUSTR("/singletons/com.sun.star.util.theMacroExpander") ),
            UNO_QUERY_THROW )->
        expandMacros( OUSTR("$URE_BIN_DIR/uno") ) );

    ::rtl::OUStringBuffer buf;
    buf.appendAscii( RTL_CONSTASCII_STRINGPARAM("uno:pipe,name=") );
    OUString pipeId( generateRandomPipeId() );
    buf.append( pipeId );
    buf.appendAscii(
        RTL_CONSTASCII_STRINGPARAM(";urp;uno.ComponentContext") );
    const OUString connectStr( buf.makeStringAndClear() );
    
    // raise core UNO process to register/run a component,
    // javavm service uses unorc next to executable to retrieve deployed
    // jar typelibs

    ::std::vector<OUString> args;
#if OSL_DEBUG_LEVEL <= 1
    args.push_back( OUSTR("--quiet") );
#endif
    args.push_back( OUSTR("--singleaccept") );
    args.push_back( OUSTR("-u") );
    args.push_back( connectStr );
    // don't inherit from unorc:
    args.push_back( OUSTR("-env:INIFILENAME=") );

    //now add the bootstrap variables which were supplied on the command line
    ::std::vector<OUString> bootvars = getCmdBootstrapVariables();
    args.insert(args.end(), bootvars.begin(), bootvars.end());
    
    oslProcess hProcess = raiseProcess(
        url, comphelper::containerToSequence(args) );
    try {
        return Reference<XComponentContext>(
            resolveUnoURL( connectStr, xContext, abortChannel.get() ),
            UNO_QUERY_THROW );
    }
    catch (...) {
        // try to terminate process:
        if ( osl_terminateProcess( hProcess ) != osl_Process_E_None )
        {
            OSL_ASSERT( false );
        }
        throw;
    }
}

//------------------------------------------------------------------------------
void BackendImpl::ComponentPackageImpl::getComponentInfo(
    ComponentBackendDb::Data * data,
    std::vector< css::uno::Reference< css::uno::XInterface > > * factories,
    Reference<XComponentContext> const & xContext )
{
    const Reference<loader::XImplementationLoader> xLoader(
        xContext->getServiceManager()->createInstanceWithContext(
            m_loader, xContext ), UNO_QUERY );
    if (! xLoader.is())
    {
        throw css::deployment::DeploymentException(
            (rtl::OUString(
                RTL_CONSTASCII_USTRINGPARAM("cannot instantiate loader ")) +
             m_loader),
            static_cast< OWeakObject * >(this), Any());
    }
    
    // HACK: highly dependent on stoc/source/servicemanager
    //       and stoc/source/implreg implementation which rely on the same
    //       services.rdb format!
    //       .../UNO/LOCATION and .../UNO/ACTIVATOR appear not to be written by
    //       writeRegistryInfo, however, but are knwon, fixed values here, so
    //       can be passed into extractComponentData
    rtl::OUString url(getURL());
    const Reference<registry::XSimpleRegistry> xMemReg(
        xContext->getServiceManager()->createInstanceWithContext(
            OUSTR("com.sun.star.registry.SimpleRegistry"), xContext ),
        UNO_QUERY_THROW );
    xMemReg->open( OUString() /* in mem */, false, true );
    xLoader->writeRegistryInfo( xMemReg->getRootKey(), OUString(), url );
    getMyBackend()->extractComponentData(
        xContext, xMemReg->getRootKey(), data, factories, &xLoader, &url);
}

// Package
//______________________________________________________________________________
//We could use here BackendImpl::hasActiveEntry. However, this check is just as well.
//And it also shows the problem if another extension has overwritten an implementation
//entry, because it contains the same service implementation
beans::Optional< beans::Ambiguous<sal_Bool> >
BackendImpl::ComponentPackageImpl::isRegistered_(
    ::osl::ResettableMutexGuard &,
    ::rtl::Reference<AbortChannel> const & abortChannel,
    Reference<XCommandEnvironment> const & )
{
    if (m_registered == REG_UNINIT)
    {
        m_registered = REG_NOT_REGISTERED;
        bool bAmbiguousComponentName = false;
        const Reference<registry::XSimpleRegistry> xRDB( getRDB_RO() );
        if (xRDB.is())
        {
            // lookup rdb for location URL:
            const Reference<registry::XRegistryKey> xRootKey(
                xRDB->getRootKey() );
            const Reference<registry::XRegistryKey> xImplKey(
                xRootKey->openKey( OUSTR("IMPLEMENTATIONS") ) );
            Sequence<OUString> implNames;
            if (xImplKey.is() && xImplKey->isValid())
                implNames = xImplKey->getKeyNames();
            OUString const * pImplNames = implNames.getConstArray();
            sal_Int32 pos = implNames.getLength();
            for ( ; pos--; )
            {
                checkAborted( abortChannel );    
                const OUString key(
                    pImplNames[ pos ] + OUSTR("/UNO/LOCATION") );
                const Reference<registry::XRegistryKey> xKey(
                    xRootKey->openKey(key) );
                if (xKey.is() && xKey->isValid()) 
                {
                    const OUString location( xKey->getAsciiValue() );
                    if (location.equalsIgnoreAsciiCase( getURL() ))
                    {
                        break;
                    }
                    else
                    {
                        //try to match only the file name
                        OUString thisUrl(getURL());
                        OUString thisFileName(thisUrl.copy(thisUrl.lastIndexOf('/')));
                        
                        OUString locationFileName(location.copy(location.lastIndexOf('/')));
                        if (locationFileName.equalsIgnoreAsciiCase(thisFileName))
                            bAmbiguousComponentName = true;
                    }
                }
            }
            if (pos >= 0)
                m_registered = REG_REGISTERED;
            else if (bAmbiguousComponentName)
                m_registered = REG_MAYBE_REGISTERED;
        }
    }

    //Different extensions can use the same service implementations. Then the extensions
    //which was installed last will overwrite the one from the other extension. That is
    //the registry will contain the path (the location) of the library or jar of the 
    //second extension. In this case isRegistered called for the lib of the first extension
    //would return "not registered". That would mean that during uninstallation 
    //XPackage::registerPackage is not called, because it just was not registered. This is,
    //however, necessary for jar files. Registering and unregistering update 
    //uno_packages/cache/registry/com.sun.star.comp.deployment.component.PackageRegistryBackend/unorc
    //Therefore, we will return always "is ambiguous" if the path of this component cannot 
    //be found in the registry and if there is another path and both have the same file name (but
    //the rest of the path is different).
    //If the caller cannot precisely determine that this package was registered, then it must
    //call registerPackage.
    sal_Bool bAmbiguous = m_registered == REG_VOID // REG_VOID == we are in the progress of unregistration
        || m_registered == REG_MAYBE_REGISTERED;
    return beans::Optional< beans::Ambiguous<sal_Bool> >(
        true /* IsPresent */,
        beans::Ambiguous<sal_Bool>(
            m_registered == REG_REGISTERED, bAmbiguous) );
}

//______________________________________________________________________________
void BackendImpl::ComponentPackageImpl::processPackage_(
    ::osl::ResettableMutexGuard &,
    bool doRegisterPackage,
    bool startup,
    ::rtl::Reference<AbortChannel> const & abortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    BackendImpl * that = getMyBackend();
    rtl::OUString url(getURL());
    if (doRegisterPackage) {
        ComponentBackendDb::Data data;
        css::uno::Reference< css::uno::XComponentContext > context;
        if (startup) {
            context = that->getComponentContext();
        } else {
            context.set(that->getObject(url), css::uno::UNO_QUERY);
            if (!context.is()) {
                context.set(
                    that->insertObject(
                        url,
                        raise_uno_process(
                            that->getComponentContext(), abortChannel)),
                    css::uno::UNO_QUERY_THROW);
            }
        }
        css::uno::Reference< css::registry::XImplementationRegistration>(
            context->getServiceManager()->createInstanceWithContext(
                rtl::OUString(
                    RTL_CONSTASCII_USTRINGPARAM(
                        "com.sun.star.registry.ImplementationRegistration")),
                context),
            css::uno::UNO_QUERY_THROW)->registerImplementation(
                m_loader, url, getRDB());
        // Only write to unorc after successful registration; it may fail if
        // there is no suitable java
        if (m_loader.equalsAsciiL(
                RTL_CONSTASCII_STRINGPARAM("com.sun.star.loader.Java2")) &&
            !jarManifestHeaderPresent(url, OUSTR("UNO-Type-Path"), xCmdEnv))
        {
            that->addToUnoRc(RCITEM_JAR_TYPELIB, url, xCmdEnv);
            data.javaTypeLibrary = true;
        }
        std::vector< css::uno::Reference< css::uno::XInterface > > factories;
        getComponentInfo(&data, &factories, context);
        if (!startup) {
            that->componentLiveInsertion(data, factories);
        }
        m_registered = REG_REGISTERED;
        that->addDataToDb(url, data);
    } else { // revoke
        m_registered = REG_VOID;
        ComponentBackendDb::Data data(that->readDataFromDb(url));
        css::uno::Reference< css::uno::XComponentContext > context(
            that->getObject(url), css::uno::UNO_QUERY);
        bool remoteContext = context.is();
        if (!remoteContext) {
            context = that->getComponentContext();
        }
        if (!startup) {
            that->componentLiveRemoval(data);
        }
        css::uno::Reference< css::registry::XImplementationRegistration >(
            context->getServiceManager()->createInstanceWithContext(
                rtl::OUString(
                    RTL_CONSTASCII_USTRINGPARAM(
                        "com.sun.star.registry.ImplementationRegistration")),
                context),
            css::uno::UNO_QUERY_THROW)->revokeImplementation(url, getRDB());
        if (data.javaTypeLibrary) {
            that->removeFromUnoRc(RCITEM_JAR_TYPELIB, url, xCmdEnv);
        }
        if (remoteContext) {
            that->releaseObject(url);
        }
        m_registered = REG_NOT_REGISTERED;
        getMyBackend()->revokeEntryFromDb(url);
    }
}

//##############################################################################
BackendImpl::TypelibraryPackageImpl::TypelibraryPackageImpl(
    ::rtl::Reference<PackageRegistryBackend> const & myBackend,
    OUString const & url, OUString const & name,
    Reference<deployment::XPackageTypeInfo> const & xPackageType,
    bool jarFile, bool bRemoved, OUString const & identifier)
    : Package( myBackend, url, name, name /* display-name */,
               xPackageType, bRemoved, identifier),
      m_jarFile( jarFile )
{
}

// Package
BackendImpl * BackendImpl::TypelibraryPackageImpl::getMyBackend() const
{
    BackendImpl * pBackend = static_cast<BackendImpl *>(m_myBackend.get());
    if (NULL == pBackend)
    {    
        //May throw a DisposedException
        check();
        //We should never get here...
        throw RuntimeException(
            OUSTR("Failed to get the BackendImpl"), 
            static_cast<OWeakObject*>(const_cast<TypelibraryPackageImpl *>(this)));
    }
    return pBackend;
}
//______________________________________________________________________________
beans::Optional< beans::Ambiguous<sal_Bool> >
BackendImpl::TypelibraryPackageImpl::isRegistered_(
    ::osl::ResettableMutexGuard &,
    ::rtl::Reference<AbortChannel> const &,
    Reference<XCommandEnvironment> const & )
{
    BackendImpl * that = getMyBackend();
    return beans::Optional< beans::Ambiguous<sal_Bool> >(
        true /* IsPresent */,
        beans::Ambiguous<sal_Bool>(
            that->hasInUnoRc(
                m_jarFile ? RCITEM_JAR_TYPELIB : RCITEM_RDB_TYPELIB, getURL() ),
            false /* IsAmbiguous */ ) );
}

//______________________________________________________________________________
void BackendImpl::TypelibraryPackageImpl::processPackage_(
    ::osl::ResettableMutexGuard &,
    bool doRegisterPackage,
    bool /*startup*/,
    ::rtl::Reference<AbortChannel> const &,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    BackendImpl * that = getMyBackend();    
    const OUString url( getURL() );

    if (doRegisterPackage)
    {    
        // live insertion:
        if (m_jarFile) {
            // xxx todo add to classpath at runtime: ???
            //SB: It is probably not worth it to add the live inserted type
            // library JAR to the UnoClassLoader in the soffice process.  Any
            // live inserted component JAR that might reference this type
            // library JAR runs in its own uno process, so there is probably no
            // Java code in the soffice process that would see any UNO types
            // introduced by this type library JAR.
        }
        else // RDB:
        {
            Reference<XComponentContext> const & xContext =
                that->getComponentContext();
            if (! m_xTDprov.is())
            {
                m_xTDprov.set( that->getObject( url ), UNO_QUERY );
                if (! m_xTDprov.is())
                {
                    const Reference<registry::XSimpleRegistry> xReg(
                        xContext->getServiceManager()
                        ->createInstanceWithContext(
                            OUSTR("com.sun.star.registry.SimpleRegistry"),
                            xContext ), UNO_QUERY_THROW );
                    xReg->open( expandUnoRcUrl(url),
                                true /* read-only */, false /* ! create */ );
                    const Any arg(xReg);
                    Reference<container::XHierarchicalNameAccess> xTDprov(
                        xContext->getServiceManager()
                        ->createInstanceWithArgumentsAndContext(
                            OUSTR("com.sun.star.comp.stoc."
                                  "RegistryTypeDescriptionProvider"),
                            Sequence<Any>( &arg, 1 ), xContext ), UNO_QUERY );
                    OSL_ASSERT( xTDprov.is() );
                    if (xTDprov.is())
                        m_xTDprov.set( that->insertObject( url, xTDprov ),
                                       UNO_QUERY_THROW );
                }
            }
            if (m_xTDprov.is()) {
                Reference<container::XSet> xSet(
                    xContext->getValueByName(
                        OUSTR("/singletons/com.sun.star."
                              "reflection.theTypeDescriptionManager") ),
                    UNO_QUERY_THROW );
                xSet->insert( Any(m_xTDprov) );
            }
        }
        
        that->addToUnoRc( m_jarFile ? RCITEM_JAR_TYPELIB : RCITEM_RDB_TYPELIB,
                          url, xCmdEnv );
    }
    else // revokePackage()
    {   
        that->removeFromUnoRc(
            m_jarFile ? RCITEM_JAR_TYPELIB : RCITEM_RDB_TYPELIB, url, xCmdEnv );
        
        // revoking types at runtime, possible, sensible?
        if (!m_xTDprov.is())
            m_xTDprov.set( that->getObject( url ), UNO_QUERY );
        if (m_xTDprov.is()) {
            // remove live:
            const Reference<container::XSet> xSet(
                that->getComponentContext()->getValueByName(
                    OUSTR("/singletons/com.sun.star."
                          "reflection.theTypeDescriptionManager") ),
                UNO_QUERY_THROW );
            xSet->remove( Any(m_xTDprov) );
            
            that->releaseObject( url );
            m_xTDprov.clear();
        }
    }
}

BackendImpl * BackendImpl::ComponentsPackageImpl::getMyBackend() const
{
    BackendImpl * pBackend = static_cast<BackendImpl *>(m_myBackend.get());
    if (NULL == pBackend)
    {
        //Throws a DisposedException
        check();
        //We should never get here...
        throw RuntimeException(
            OUSTR("Failed to get the BackendImpl"),
            static_cast<OWeakObject*>(const_cast<ComponentsPackageImpl *>(this)));
    }
    return pBackend;
}

beans::Optional< beans::Ambiguous<sal_Bool> >
BackendImpl::ComponentsPackageImpl::isRegistered_(
    ::osl::ResettableMutexGuard &,
    ::rtl::Reference<AbortChannel> const &,
    Reference<XCommandEnvironment> const & )
{
    return beans::Optional< beans::Ambiguous<sal_Bool> >(
        true,
        beans::Ambiguous<sal_Bool>(
            getMyBackend()->hasInUnoRc(RCITEM_COMPONENTS, getURL()), false));
}

void BackendImpl::ComponentsPackageImpl::processPackage_(
    ::osl::ResettableMutexGuard &,
    bool doRegisterPackage,
    bool startup,
    ::rtl::Reference<AbortChannel> const & abortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    BackendImpl * that = getMyBackend();
    rtl::OUString url(getURL());
    if (doRegisterPackage) {
        ComponentBackendDb::Data data;
        data.javaTypeLibrary = false;
                css::uno::Reference< css::uno::XComponentContext > context;
        if (startup) {
            context = that->getComponentContext();
        } else {
            context.set(that->getObject(url), css::uno::UNO_QUERY);
            if (!context.is()) {
                context.set(
                    that->insertObject(
                        url,
                        raise_uno_process(
                            that->getComponentContext(), abortChannel)),
                    css::uno::UNO_QUERY_THROW);
            }
        }

        std::vector< css::uno::Reference< css::uno::XInterface > > factories;

        css::uno::Reference< css::registry::XSimpleRegistry > registry(
            css::uno::Reference< css::lang::XMultiComponentFactory >(
                that->getComponentContext()->getServiceManager(),
                css::uno::UNO_SET_THROW)->createInstanceWithContext(
                    rtl::OUString(
                        RTL_CONSTASCII_USTRINGPARAM(
                            "com.sun.star.registry.SimpleRegistry")),
                    that->getComponentContext()),
            css::uno::UNO_QUERY_THROW);
        registry->open(expandUnoRcUrl(url), true, false);
        getMyBackend()->extractComponentData(
            context,
            that->openRegistryKey(
                registry->getRootKey(),
                rtl::OUString(RTL_CONSTASCII_USTRINGPARAM("IMPLEMENTATIONS"))),
            &data, &factories, 0, 0);
        registry->close();
        if (!startup) {
            that->componentLiveInsertion(data, factories);
        }
        that->addDataToDb(url, data);
        that->addToUnoRc(RCITEM_COMPONENTS, url, xCmdEnv);
    } else { // revoke
        that->removeFromUnoRc(RCITEM_COMPONENTS, url, xCmdEnv);
        if (!startup) {
            that->componentLiveRemoval(that->readDataFromDb(url));
        }
        that->releaseObject(url);
        that->revokeEntryFromDb(url);
    }
}

BackendImpl::ComponentsPackageImpl::ComponentsPackageImpl(
    ::rtl::Reference<PackageRegistryBackend> const & myBackend,
    OUString const & url, OUString const & name,
    Reference<deployment::XPackageTypeInfo> const & xPackageType,
    bool bRemoved, OUString const & identifier)
    : Package( myBackend, url, name, name /* display-name */,
               xPackageType, bRemoved, identifier)
{}

} // anon namespace

namespace sdecl = comphelper::service_decl;
sdecl::class_<BackendImpl, sdecl::with_args<true> > serviceBI;
extern sdecl::ServiceDecl const serviceDecl(
    serviceBI,
    IMPLEMENTATION_NAME,
    BACKEND_SERVICE_NAME );

} // namespace component
} // namespace backend
} // namespace dp_registry


