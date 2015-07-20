#pragma once

#include <SoyOpenglContext.h>
#include <SoyFilesystem.h>
#include <TJob.h>


class TFilterWindow;
class TFilter;
class TFilterFrame;
class TFilterStageRuntimeData;



class TFilterStage
{
public:
	TFilterStage(const std::string& Name,TFilter& Filter);
	
	virtual bool		Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data)=0;

	bool				operator==(const std::string& Name) const	{	return mName == Name;	}

public:
	SoyEvent<TFilterStage&>	mOnChanged;
	std::string				mName;
	TFilter&				mFilter;
};

class TFilterStageRuntimeData
{
public:
	virtual bool				SetUniform(const std::string& StageName,Opengl::TShaderState& Shader,Opengl::TUniform& Uniform,TFilter& Filter)=0;
	virtual Opengl::TTexture	GetTexture()=0;
};


class TFilterStage_ShaderBlit : public TFilterStage
{
public:
	TFilterStage_ShaderBlit(const std::string& Name,const std::string& VertFilename,const std::string& FragFilename,const Opengl::TGeometryVertex& BlitVertexDescription,TFilter& Filter);
	
	void				Reload();
	virtual bool		Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data) override;
	
	bool				operator==(const std::string& Name) const	{	return mName == Name;	}
	
public:
	std::string				mVertFilename;
	std::string				mFragFilename;
	Soy::TFileWatch			mVertFileWatch;
	Soy::TFileWatch			mFragFileWatch;
	Opengl::GlProgram		mShader;
	Opengl::TGeometryVertex	mBlitVertexDescription;
};

class TFilterStageRuntimeData_ShaderBlit : public TFilterStageRuntimeData
{
public:
	virtual bool				SetUniform(const std::string& StageName,Opengl::TShaderState& Shader,Opengl::TUniform& Uniform,TFilter& Filter) override;
	virtual Opengl::TTexture	GetTexture() override	{	return mTexture;	}

public:
	Opengl::TTexture		mTexture;
};



class TFilterStage_ReadPixels : public TFilterStage
{
public:
	TFilterStage_ReadPixels(const std::string& Name,const std::string& SourceStage,TFilter& Filter);

	virtual bool		Execute(TFilterFrame& Frame,std::shared_ptr<TFilterStageRuntimeData>& Data);
	
public:
	std::string			mSourceStage;
};

class TFilterStageRuntimeData_ReadPixels : public TFilterStageRuntimeData
{
public:
	virtual bool				SetUniform(const std::string& StageName,Opengl::TShaderState& Shader,Opengl::TUniform& Uniform,TFilter& Filter) override;
	virtual Opengl::TTexture	GetTexture() override	{	return Opengl::TTexture();	}
	
public:
	SoyPixels			mPixels;
};


class TFilterFrame
{
public:
	Opengl::TTexture						mFrame;				//	first input
	std::map<std::string,std::shared_ptr<TFilterStageRuntimeData>>	mStageData;
	
	bool		Run(std::ostream& Error,TFilter& Filter);
	
	bool		SetUniform(Opengl::TShaderState& Shader,Opengl::TUniform& Uniform,TFilter& Filter);
	std::shared_ptr<TFilterStageRuntimeData>	GetData(const std::string& StageName);

public:
	static bool	SetTextureUniform(Opengl::TShaderState& Shader,Opengl::TUniform& Uniform,Opengl::TTexture& Texture,const std::string& TextureName);
};



class TFilterMeta
{
public:
	TFilterMeta(const std::string& Name) :
		mName	( Name )
	{
	}

	bool					operator==(const std::string& Name) const	{	return mName == Name;	}
	
	std::string				mName;
};





class TFilter : public TFilterMeta
{
public:
	static const char* FrameSourceName;
	
public:
	TFilter(const std::string& Name);
	
	void					QueueRun(SoyTime Frame);
	Opengl::TContext&		GetContext();			//	in the window
	
	void					LoadFrame(std::shared_ptr<SoyPixels>& Pixels,SoyTime Time);	//	load pixels into [new] frame
	void					OnFrameChanged(SoyTime Frame)	{	QueueRun(Frame);	}

	void					AddStage(const std::string& Name,const TJobParams& Params);
	void					OnStagesChanged();
	void					OnUniformChanged(const std::string& Name);

	virtual bool			SetUniform(Opengl::TShaderState& Shader,Opengl::TUniform& Uniform)
	{
		return false;
	}
	virtual bool			SetUniform(TJobParam& Param)
	{
		throw Soy::AssertException( std::string("No known uniform ")+Param.GetKey() );
	}

public:
	SoyEvent<std::tuple<SoyTime,TFilterFrame&>>		mOnRunCompleted;
	SoyEvent<std::tuple<SoyTime,TFilterFrame&>>		mOnRunIncomplete;
	std::shared_ptr<TFilterWindow>					mWindow;		//	this also contains our context
	std::map<SoyTime,std::shared_ptr<TFilterFrame>>	mFrames;
	Array<std::shared_ptr<TFilterStage>>			mStages;
	Opengl::TGeometry		mBlitQuad;
};


class TExtractedPlayer
{
public:
	vec2f	mUv;
	vec3f	mRgb;	//	convert this to actual data based on other uniforms
};

class TExtractedFrame
{
public:
	Array<TExtractedPlayer>	mPlayers;
	SoyTime					mTime;
};

class TPlayerFilter : public TFilter
{
public:
	TPlayerFilter(const std::string& Name);
	
	virtual bool			SetUniform(Opengl::TShaderState& Shader,Opengl::TUniform& Uniform) override;
	virtual bool			SetUniform(TJobParam& Param) override;
	
	void					ExtractPlayers(std::tuple<SoyTime,TFilterFrame&>& Frame);
	
public:
	SoyEvent<TExtractedFrame>	mOnExtractedPlayers;
	BufferArray<vec2f,4>		mPitchCorners;
};
