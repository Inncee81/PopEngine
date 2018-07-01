#include "TApiCommon.h"
#include <SoyDebug.h>
#include <SoyImage.h>
#include <SoyFilesystem.h>
#include <SoyStream.h>
#include <SoyOpengl.h>

using namespace v8;

const char Debug_FunctionName[] = "Debug";
const char LoadFileAsString_FunctionName[] = "LoadFileAsString";

const char LoadFile_FunctionName[] = "Load";
const char Alloc_FunctionName[] = "Create";
const char GetWidth_FunctionName[] = "GetWidth";
const char GetHeight_FunctionName[] = "GetHeight";
const char SetLinearFilter_FunctionName[] = "SetLinearFilter";

static v8::Local<v8::Value> Debug(v8::CallbackInfo& Params);
static v8::Local<v8::Value> LoadFileAsString(v8::CallbackInfo& Params);


void ApiCommon::Bind(TV8Container& Container)
{
	//  load api's before script & executions
	Container.BindGlobalFunction<Debug_FunctionName>(Debug);
	Container.BindGlobalFunction<LoadFileAsString_FunctionName>(LoadFileAsString);
	Container.BindObjectType("Image", TImageWrapper::CreateTemplate );
}

static Local<Value> Debug(CallbackInfo& Params)
{
	auto& args = Params.mParams;
	
	if (args.Length() < 1)
	{
		throw Soy::AssertException("log() with no args");
	}
	
	HandleScope scope(Params.mIsolate);
	for ( auto i=0;	i<args.Length();	i++ )
	{
		auto arg = args[i];
		String::Utf8Value value(arg);
		std::Debug << *value << std::endl;
	}
	
	return Undefined(Params.mIsolate);
}


static Local<Value> LoadFileAsString(CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	if (Arguments.Length() < 1)
		throw Soy::AssertException("LoadFileAsString(Filename) with no args");

	auto Filename = v8::GetString( Arguments[0] );
	std::string Contents;
	if ( !Soy::FileToString( Filename, Contents) )
	{
		std::stringstream Error;
		Error << "Failed to read " << Filename;
		throw Soy::AssertException( Error.str() );
	}
	
	auto ContentsString = v8::GetString( Params.GetIsolate(), Contents );
	return ContentsString;
}




void TImageWrapper::Constructor(const v8::FunctionCallbackInfo<v8::Value>& Arguments)
{
	auto* Isolate = Arguments.GetIsolate();
	
	if ( !Arguments.IsConstructCall() )
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, "Expecting to be used as constructor. new Window(Name);"));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
	
	auto This = Arguments.This();
	
	//	gr: auto catch this
	try
	{
		auto& Container = GetObject<TV8Container>( Arguments.Data() );
		
		//	gr: this should be OWNED by the context (so we can destroy all c++ objects with the context)
		//		but it also needs to know of the V8container to run stuff
		//		cyclic hell!
		auto* NewImage = new TImageWrapper();
		NewImage->mHandle.Reset( Isolate, Arguments.This() );
		NewImage->mContainer = &Container;
		This->SetInternalField( 0, External::New( Arguments.GetIsolate(), NewImage ) );
		// return the new object back to the javascript caller
		Arguments.GetReturnValue().Set( This );
		
		//	construct with filename
		if ( Arguments[0]->IsString() )
		{
			auto ThisLoadFile = [&](v8::CallbackInfo& Args)
			{
				return NewImage->LoadFile(Args);
			};
			CallFunc( ThisLoadFile, Arguments, Container );
		}
		
		//	construct with size
		if ( Arguments[0]->IsArray() )
		{
			auto ThisAlloc = [&](v8::CallbackInfo& Args)
			{
				return NewImage->Alloc(Args);
			};
			CallFunc( ThisAlloc, Arguments, Container );
		}
		
	}
	catch(std::exception& e)
	{
		auto Exception = Isolate->ThrowException(String::NewFromUtf8( Isolate, e.what() ));
		Arguments.GetReturnValue().Set(Exception);
		return;
	}
}

Local<FunctionTemplate> TImageWrapper::CreateTemplate(TV8Container& Container)
{
	auto* Isolate = Container.mIsolate;
	
	//	pass the container around
	auto ContainerHandle = External::New( Isolate, &Container );
	auto ConstructorFunc = FunctionTemplate::New( Isolate, Constructor, ContainerHandle );
	
	//	https://github.com/v8/v8/wiki/Embedder's-Guide
	//	1 field to 1 c++ object
	//	gr: we can just use the template that's made automatically and modify that!
	//	gr: prototypetemplate and instancetemplate are basically the same
	//		but for inheritance we may want to use prototype
	//		https://groups.google.com/forum/#!topic/v8-users/_i-3mgG5z-c
	auto InstanceTemplate = ConstructorFunc->InstanceTemplate();
	
	//	[0] object
	//	[1] container
	InstanceTemplate->SetInternalFieldCount(2);
	
	Container.BindFunction<LoadFile_FunctionName>( InstanceTemplate, TImageWrapper::LoadFile );
	Container.BindFunction<Alloc_FunctionName>( InstanceTemplate, TImageWrapper::Alloc );
	Container.BindFunction<GetWidth_FunctionName>( InstanceTemplate, TImageWrapper::GetWidth );
	Container.BindFunction<GetHeight_FunctionName>( InstanceTemplate, TImageWrapper::GetHeight );
	Container.BindFunction<SetLinearFilter_FunctionName>( InstanceTemplate, TImageWrapper::SetLinearFilter );

	return ConstructorFunc;
}


v8::Local<v8::Value> TImageWrapper::Alloc(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TImageWrapper>( ThisHandle );

	BufferArray<int,2> IntArray;
	if ( Arguments[0]->IsArray() )
	{
		v8::EnumArray( Arguments[0], GetArrayBridge(IntArray) );
	}
	else if ( Arguments[0]->IsNumber() && Arguments[1]->IsNumber() )
	{
		v8::EnumArray( Arguments[0], GetArrayBridge(IntArray) );
		v8::EnumArray( Arguments[1], GetArrayBridge(IntArray) );
	}
	else
		throw Soy::AssertException("Invalid params Alloc(width,height) or Alloc( [width,height] )");

	auto Width = IntArray[0];
	auto Height = IntArray[1];
	auto Format = SoyPixelsFormat::Type::RGBA;
	This.mPixels.reset( new SoyPixels( SoyPixelsMeta( Width, Height, Format ) ) );

	return v8::Undefined(Params.mIsolate);
}


v8::Local<v8::Value> TImageWrapper::LoadFile(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TImageWrapper>( ThisHandle );

	//	if first arg is filename...
	
	std::string Filename( *String::Utf8Value(Arguments[0]) );
	This.DoLoadFile( Filename );
	return v8::Undefined(Params.mIsolate);
}

void TImageWrapper::DoLoadFile(const std::string& Filename)
{
	//	load file
	Array<char> Bytes;
	Soy::FileToArray( GetArrayBridge(Bytes), Filename );
	TStreamBuffer BytesBuffer;
	BytesBuffer.Push( GetArrayBridge(Bytes) );

	//	alloc pixels
	std::shared_ptr<SoyPixels> NewPixels( new SoyPixels );
	
	if ( Soy::StringEndsWith( Filename, Png::FileExtensions, false ) )
	{
		Png::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}
	
	if ( Soy::StringEndsWith( Filename, Jpeg::FileExtensions, false ) )
	{
		Jpeg::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}
	
	if ( Soy::StringEndsWith( Filename, Gif::FileExtensions, false ) )
	{
		Gif::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}
	
	if ( Soy::StringEndsWith( Filename, Tga::FileExtensions, false ) )
	{
		Tga::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}
	
	if ( Soy::StringEndsWith( Filename, Bmp::FileExtensions, false ) )
	{
		Bmp::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}
	
	if ( Soy::StringEndsWith( Filename, Psd::FileExtensions, false ) )
	{
		Psd::Read( *NewPixels, BytesBuffer );
		mPixels = NewPixels;
		return;
	}


	
	throw Soy::AssertException( std::string("Unhandled image file extension of ") + Filename );
}


void TImageWrapper::DoSetLinearFilter(bool LinearFilter)
{
	//	for now, only allow this pre-creation
	//	what we could do, is queue an opengl job. but if we're IN a job now, it'll set it too late
	//	OR, queue it to be called before next GetTexture()
	if ( mOpenglTexture != nullptr )
		throw Soy::AssertException("Cannot change linear filter setting if texture is already created");

	mLinearFilter = LinearFilter;
}

v8::Local<v8::Value> TImageWrapper::GetWidth(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TImageWrapper>( ThisHandle );

	size_t Width = 0;
	if ( This.mPixels )
		Width = This.mPixels->GetWidth();
	else if ( This.mOpenglTexture )
		Width = This.mOpenglTexture->GetWidth();
	else
		throw Soy::AssertException("Image not allocated");
	
	return Number::New( Params.mIsolate, Width );
}


v8::Local<v8::Value> TImageWrapper::GetHeight(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TImageWrapper>( ThisHandle );
	
	size_t Height = 0;
	if ( This.mPixels )
		Height = This.mPixels->GetWidth();
	else if ( This.mOpenglTexture )
		Height = This.mOpenglTexture->GetWidth();
	else
		throw Soy::AssertException("Image not allocated");
	
	return Number::New( Params.mIsolate, Height );
}

v8::Local<v8::Value> TImageWrapper::SetLinearFilter(const v8::CallbackInfo& Params)
{
	auto& Arguments = Params.mParams;
	
	auto ThisHandle = Arguments.This()->GetInternalField(0);
	auto& This = v8::GetObject<TImageWrapper>( ThisHandle );
	
	if ( Arguments.Length() != 1 )
		throw Soy::AssertException( "SetLinearFilter(true/false) expected 1 argument");
	
	if ( !Arguments[0]->IsBoolean() )
		throw Soy::AssertException( "SetLinearFilter(true/false) expected boolean argument");

	auto ValueBool = Local<v8::Boolean>::Cast( Arguments[0] );
	auto LinearFilter = ValueBool->Value();
	This.DoSetLinearFilter( LinearFilter );

	return v8::Undefined(Params.mIsolate);
}

void TImageWrapper::GetTexture(std::function<void()> OnTextureLoaded,std::function<void(const std::string&)> OnError)
{
	//	already created
	if ( mOpenglTexture != nullptr )
	{
		OnTextureLoaded();
		return;
	}
	
	if ( !mPixels )
		throw Soy::AssertException("Trying to get opengl texture when we have no pixels");
	
	//	gr: this will need to be on the context's thread
	//		need to fail here if we're not
	try
	{
		mOpenglTexture.reset( new Opengl::TTexture( mPixels->GetMeta(), GL_TEXTURE_2D ) );
		mOpenglTexture->SetFilter( mLinearFilter );
		mOpenglTexture->SetRepeat( mRepeating );

		SoyGraphics::TTextureUploadParams UploadParams;
		mOpenglTexture->Write( *mPixels, UploadParams );
		OnTextureLoaded();
	}
	catch(std::exception& e)
	{
		OnError( e.what() );
	}
}

const Opengl::TTexture& TImageWrapper::GetTexture()
{
	if ( !mOpenglTexture )
		throw Soy::AssertException("Image missing opengl texture. Accessing before generating.");
	
	return *mOpenglTexture;
}


