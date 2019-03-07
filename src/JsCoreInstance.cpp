#include "JsCoreInstance.h"
#include "SoyAssert.h"
#include "SoyFilesystem.h"
#include "TApiCommon.h"





JSObjectRef GetObject(JSContextRef Context,JSValueRef Value)
{
	if ( !JSValueIsObject( Context, Value ) )
		throw Soy::AssertException("Value is not object");
	return const_cast<JSObjectRef>( Value );
}

JsCore::TPromise::TPromise(JSContextRef Context,JSValueRef Promise,JSValueRef ResolveFunc,JSValueRef RejectFunc) :
	mPromise	( GetObject(Context,Promise) ),
	mResolve	( Context, ResolveFunc ),
	mReject		( Context, RejectFunc )
{
}


JsCore::TFunction::TFunction(JSContextRef Context,JSValueRef Value) :
	mContext	( Context )
{
	auto ValueType = JSValueGetType( Context, Value );
	if ( ValueType != kJSTypeObject )
		throw Soy::AssertException("Value for TFunciton is not an object");
	
	mFunctionObject = const_cast<JSObjectRef>( Value );
	
	if ( !JSObjectIsFunction(Context, mFunctionObject) )
		throw Soy::AssertException("Object should be function");
}
/*
JSValueRef JsCore::TFunction::TFunction::Call(JSObjectRef This,JSValueRef Arg0)
{
	if ( This == nullptr )
		This = JSContextGetGlobalObject( mContext );
	
	JSValueRef Exception = nullptr;
	auto Result = JSObjectCallAsFunction( mContext, mFunctionObject, This, 1, &Arg0, &Exception );
	
	if ( Exception!=nullptr )
		throw Soy::AssertException( JsCore::HandleToString( mContext, Exception ) );
	
	return Result;
}


JsCore::TPromise JSCreatePromise(JSContextRef Context)
{
	static std::shared_ptr<JsCore::TFunction> MakePromiseFunction;
	
	if ( !MakePromiseFunction )
	{
		auto* MakePromiseFunctionSource =  R"V0G0N(
		
		let MakePromise = function()
		{
			var PromData = {};
			var prom = new Promise( function(Resolve,Reject) { PromData.Resolve = Resolve; PromData.Reject = Reject; } );
			PromData.Promise = prom;
			prom.Resolve = PromData.Resolve;
			prom.Reject = PromData.Reject;
			return prom;
		}
		MakePromise;
		//MakePromise();
		)V0G0N";
		
		JSStringRef MakePromiseFunctionSourceString = JSStringCreateWithUTF8CString(MakePromiseFunctionSource);
		JSValueRef Exception = nullptr;
		
		auto MakePromiseFunctionValue = JSEvaluateScript( Context, MakePromiseFunctionSourceString, nullptr, nullptr, 0, &Exception );
		if ( Exception!=nullptr )
			std::Debug << "An exception" << JsCore::HandleToString( Context, Exception ) << std::endl;
		
		MakePromiseFunction.reset( new JsCore::TFunction( Context, MakePromiseFunctionValue ) );
	}
	
	auto This = JSContextGetGlobalObject( Context );
	auto NewPromiseHandle = MakePromiseFunction->Call();
	
	auto Type = JSValueGetType( Context, NewPromiseHandle );
	
	auto NewPromiseObject = const_cast<JSObjectRef>(NewPromiseHandle);
	JSValueRef Exception = nullptr;
	auto Resolve = const_cast<JSObjectRef>(JSObjectGetProperty( Context, NewPromiseObject, JSStringCreateWithUTF8CString("Resolve"), &Exception ) );
	auto Reject = const_cast<JSObjectRef>(JSObjectGetProperty( Context, NewPromiseObject, JSStringCreateWithUTF8CString("Reject"), &Exception ) );
	
	JsCore::TPromise Promise( Context, NewPromiseObject, Resolve, Reject );
	
	return Promise;
}

*/
std::string	JsCore::GetString(JSContextRef Context,JSStringRef Handle)
{
	size_t maxBufferSize = JSStringGetMaximumUTF8CStringSize(Handle);
	char utf8Buffer[maxBufferSize];
	size_t bytesWritten = JSStringGetUTF8CString(Handle, utf8Buffer, maxBufferSize);
	//	the last byte is a null \0 which std::string doesn't need.
	std::string utf_string = std::string(utf8Buffer, bytesWritten -1);
	return utf_string;
}

std::string	JsCore::GetString(JSContextRef Context,JSValueRef Handle)
{
	//	convert to string
	JSValueRef Exception = nullptr;
	auto StringJs = JSValueToStringCopy( Context, Handle, &Exception );
	return GetString( Context, StringJs );
}


int32_t JsCore::GetInt(JSContextRef Context,JSValueRef Handle)
{
	//	convert to string
	JSValueRef Exception = nullptr;
	auto DoubleJs = JSValueToNumber( Context, Handle, &Exception );

	auto Int = static_cast<int32_t>( DoubleJs );
	return Int;
}


float JsCore::GetFloat(JSContextRef Context,JSValueRef Handle)
{
	//	convert to string
	JSValueRef Exception = nullptr;
	auto DoubleJs = JSValueToNumber( Context, Handle, &Exception );
	auto Float = static_cast<float>( DoubleJs );
	return Float;
}

bool JsCore::GetBool(JSContextRef Context,JSValueRef Handle)
{
	//	convert to string
	auto Bool = JSValueToBoolean( Context, Handle );
	return Bool;
}

JSStringRef JsCore::GetString(JSContextRef Context,const std::string& String)
{
	auto Handle = JSStringCreateWithUTF8CString( String.c_str() );
	return Handle;
}

	

JsCore::TInstance::TInstance(const std::string& RootDirectory,const std::string& ScriptFilename) :
	mContextGroup	( JSContextGroupCreate() ),
	mRootDirectory	( RootDirectory )
{
	if ( !mContextGroup )
		throw Soy::AssertException("JSContextGroupCreate failed");
	
	
	//	bind first
	try
	{
		//	create a context
		mContext = CreateContext();
		
		ApiPop::Bind( *mContext );
		/*
		ApiOpengl::Bind( *mV8Container );
		ApiOpencl::Bind( *mV8Container );
		ApiDlib::Bind( *mV8Container );
		ApiMedia::Bind( *mV8Container );
		ApiWebsocket::Bind( *mV8Container );
		ApiHttp::Bind( *mV8Container );
		ApiSocket::Bind( *mV8Container );
		
		//	gr: start the thread immediately, there should be no problems having the thread running before queueing a job
		this->Start();
		*/
		std::string BootupSource;
		Soy::FileToString( mRootDirectory + ScriptFilename, BootupSource );
		/*
		auto* Container = mV8Container.get();
		auto LoadScript = [=](v8::Local<v8::Context> Context)
		{
			Container->LoadScript( Context, BootupSource, ScriptFilename );
		};
		
		mV8Container->QueueScoped( LoadScript );
		 */
		mContext->LoadScript( BootupSource, ScriptFilename );
	}
	catch(std::exception& e)
	{
		//	clean up
		mContext.reset();
		throw;
	}
}

JsCore::TInstance::~TInstance()
{
	JSContextGroupRelease(mContextGroup);
}

std::shared_ptr<JsCore::TContext> JsCore::TInstance::CreateContext()
{
	JSClassRef Global = nullptr;
	
	auto Context = JSGlobalContextCreateInGroup( mContextGroup, Global );
	std::shared_ptr<JsCore::TContext> pContext( new TContext( *this, Context, mRootDirectory ) );
	//mContexts.PushBack( pContext );
	return pContext;
}


void JsCore::ThrowException(JSContextRef Context,JSValueRef ExceptionHandle,const std::string& ThrowContext)
{
	auto ExceptionType = JSValueGetType( Context, ExceptionHandle );
	//	not an exception
	if ( ExceptionType == kJSTypeUndefined || ExceptionType == kJSTypeNull )
		return;

	std::stringstream Error;
	auto ExceptionString = GetString( Context, ExceptionHandle );
	Error << "Exception in " << ThrowContext << ": " << ExceptionString;
	throw Soy::AssertException(Error.str());
}




JsCore::TContext::TContext(TInstance& Instance,JSGlobalContextRef Context,const std::string& RootDirectory) :
	mInstance		( Instance ),
	mContext		( Context ),
	mRootDirectory	( RootDirectory )
{
}

JsCore::TContext::~TContext()
{
	JSGlobalContextRelease( mContext );
}

void JsCore::TContext::LoadScript(const std::string& Source,const std::string& Filename)
{
	auto ThisHandle = JSObjectRef(nullptr);
	auto SourceJs = JSStringCreateWithUTF8CString(Source.c_str());
	auto FilenameJs = JSStringCreateWithUTF8CString(Filename.c_str());
	auto LineNumber = 0;
	JSValueRef Exception = nullptr;
	auto ResultHandle = JSEvaluateScript( mContext, SourceJs, ThisHandle, FilenameJs, LineNumber, &Exception );
	ThrowException(Exception);
	
}




JsCore::TObject::TObject(JSContextRef Context,JSObjectRef This) :
	mContext	( Context )
{
	if ( !mContext )
		throw Soy::AssertException("Null context for TObject");

	if ( This == nullptr )
		This = JSContextGetGlobalObject( mContext );
	if ( !mContext )
		throw Soy::AssertException("This is null for TObject");
}


JSValueRef JsCore::TObject::GetMember(const std::string& MemberName)
{
	//	keep splitting the name so we can get Pop.Input.Cat
	TObject This = *this;

	//	leaf = final name
	auto LeafName = MemberName;
	while ( MemberName.length() > 0 )
	{
		auto ChildName = Soy::StringPopUntil( LeafName, '.', false, false );
		if ( ChildName.length() == 0 )
			break;

		auto Child = This.GetObject(ChildName);
		This = Child;
	}

	JSValueRef Exception = nullptr;
	auto PropertyName = JsCore::GetString( mContext, MemberName );
	auto Property = JSObjectGetProperty( mContext, This.mThis, PropertyName, &Exception );
	ThrowException( mContext, Exception );
	return Property;	//	we return null/undefineds
}

JsCore::TObject JsCore::TObject::GetObject(const std::string& MemberName)
{
	auto Value = GetMember( MemberName );
	JSValueRef Exception = nullptr;
	auto Object = JSValueToObject( mContext, Value, &Exception );
	JsCore::ThrowException( mContext, Exception, MemberName );
	return TObject( mContext, Object );
}

std::string JsCore::TObject::GetString(const std::string& MemberName)
{
	auto Value = GetMember( MemberName );
	JSValueRef Exception = nullptr;
	auto StringHandle = JSValueToStringCopy( mContext, Value, &Exception );
	JsCore::ThrowException( mContext, Exception, MemberName );
	auto String = JsCore::GetString( mContext, StringHandle );
	return String;
}

uint32_t JsCore::TObject::GetInt(const std::string& MemberName)
{
	auto Value = GetMember( MemberName );
	JSValueRef Exception = nullptr;
	auto Number = JSValueToNumber( mContext, Value, &Exception );
	JsCore::ThrowException( mContext, Exception, MemberName );
	
	//	convert this double to an int!
	auto ValueInt = static_cast<uint32_t>(Number);
	return ValueInt;
}

float JsCore::TObject::GetFloat(const std::string& MemberName)
{
	auto Value = GetMember( MemberName );
	JSValueRef Exception = nullptr;
	auto Number = JSValueToNumber( mContext, Value, &Exception );
	JsCore::ThrowException( mContext, Exception, MemberName );
	
	//	convert this double to an int!
	auto Valuef = static_cast<float>(Number);
	return Valuef;
}


void JsCore::TObject::SetObject(const std::string& Name,const TObject& Object)
{
	SetMember( Name, Object.mThis );
}

void JsCore::TObject::SetMember(const std::string& Name,JSValueRef Value)
{
	auto NameJs = JsCore::GetString( mContext, Name );
	JSPropertyAttributes Attribs;
	JSValueRef Exception = nullptr;
	JSObjectSetProperty( mContext, mThis, NameJs, Value, Attribs, &Exception );
	ThrowException( mContext, Exception );
}



JsCore::TObject JsCore::TContext::CreateObjectInstance(const std::string& ObjectTypeName)
{
	//	create basic object
	if ( ObjectTypeName.length() == 0 || ObjectTypeName == "Object" )
	{
		JSClassRef Default = nullptr;
		void* Data = nullptr;
		auto NewObject = JSObjectMake( mContext, Default, Data );
		return TObject( mContext, NewObject );
	}
	
	//	find template
	auto* pObjectTemplate = mObjectTemplates.Find( ObjectTypeName );
	if ( !pObjectTemplate )
	{
		std::stringstream Error;
		Error << "Unknown object typename ";
		Error << ObjectTypeName;
		auto ErrorStr = Error.str();
		throw Soy::AssertException(ErrorStr);
	}
	
	//	instance new one
	auto& ObjectTemplate = *pObjectTemplate;
	auto& Class = ObjectTemplate.mClass;
	void* Data = nullptr;
	auto NewObject = JSObjectMake( mContext, Class, Data );
	return TObject( mContext, NewObject );
}


void JsCore::TContext::BindRawFunction(const std::string& FunctionName,const std::string& ParentObjectName,JSObjectCallAsFunctionCallback FunctionPtr)
{
	auto This = GetGlobalObject( ParentObjectName );

	auto FunctionNameJs = JsCore::GetString( mContext, FunctionName );
	JSValueRef Exception = nullptr;
	auto FunctionHandle = JSObjectMakeFunctionWithCallback( mContext, FunctionNameJs, FunctionPtr );
	ThrowException(Exception);
	TFunction Function( mContext, FunctionHandle );
	This.SetFunction( FunctionName, Function );
}
/*
JSValueRef JsCore::TContext::CallFunc(std::function<JSValueRef(TCallbackInfo&)> Function,JSContextRef Context,JSObjectRef FunctionJs,JSObjectRef This,size_t ArgumentCount,const JSValueRef Arguments[],JSValueRef& Exception)
{
	try
	{
		TCallbackInfo CallbackInfo(mInstance);
		CallbackInfo.mContext = mContext;//Context;
		CallbackInfo.mThis = This;
		for ( auto a=0;	a<ArgumentCount;	a++ )
		{
			CallbackInfo.mArguments.PushBack( Arguments[a] );
		}
		auto Result = Function( CallbackInfo );
		return Result;
	}
	catch (std::exception& e)
	{
		auto ExceptionStr = JSStringCreateWithUTF8CString( e.what() );
		Exception = JSValueMakeString( Context, ExceptionStr );
		return JSValueMakeUndefined( Context );
	}
}
*/

/*
JSValueRef ObjectCallAsFunctionCallback(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception) {
	cout << "Hello World" << endl;
	return JSValueMakeUndefined(ctx);
}


JsCore::TInstance::
{
	JSObjectRef globalObject = JSContextGetGlobalObject(globalContext);
	
	JSStringRef logFunctionName = JSStringCreateWithUTF8CString("log");
	JSObjectRef functionObject = JSObjectMakeFunctionWithCallback(globalContext, logFunctionName, &ObjectCallAsFunctionCallback);
	
	JSObjectSetProperty(globalContext, globalObject, logFunctionName, functionObject, kJSPropertyAttributeNone, nullptr);
	
	JSStringRef logCallStatement = JSStringCreateWithUTF8CString("log()");
	
	JSEvaluateScript(globalContext, logCallStatement, nullptr, nullptr, 1,nullptr);
	
 
	JSGlobalContextRelease(globalContext);
	JSStringRelease(logFunctionName);
	JSStringRelease(logCallStatement);
	}

*/


std::string JsCore::TCallback::GetArgumentString(size_t Index)
{
	auto Handle = mArguments[Index];
	auto String = JsCore::GetString( mContext.mContext, Handle );
	return String;
}



bool JsCore::TCallback::GetArgumentBool(size_t Index)
{
	auto Handle = mArguments[Index];
	auto Value = JsCore::GetBool( mContext.mContext, Handle );
	return Value;
}

int32_t JsCore::TCallback::GetArgumentInt(size_t Index)
{
	auto Handle = mArguments[Index];
	auto Value = JsCore::GetInt( mContext.mContext, Handle );
	return Value;
}

float JsCore::TCallback::GetArgumentFloat(size_t Index)
{
	auto Handle = mArguments[Index];
	auto Value = JsCore::GetFloat( mContext.mContext, Handle );
	return Value;
}



JsCore::TObject JsCore::TContext::GetGlobalObject(const std::string& ObjectName)
{
	JsCore::TObject Global( mContext, nullptr );
	if ( ObjectName.length() == 0 )
		return Global;
	auto Child = Global.GetObject( ObjectName );
	return Child;
}


void JsCore::TContext::CreateGlobalObjectInstance(const std::string& ObjectType,const std::string&  Name)
{
	auto NewObject = CreateObjectInstance( ObjectType );
	auto ParentName = Name;
	auto ObjectName = Soy::StringPopRight( ParentName, '.' );
	auto ParentObject = GetGlobalObject( ParentName );
	ParentObject.SetObject( ObjectName, NewObject );
}



