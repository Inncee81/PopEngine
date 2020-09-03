#include "SoyGui.h"
#include <stdlib.h>
#include "LinuxDRM/esUtil.h"

class Platform::TWindow : public SoyWindow
{
public:
	TWindow( const std::string& Name );
	
	virtual Soy::Rectx<int32_t>		GetScreenRect() override;

	virtual void					SetFullscreen(bool Fullscreen) override;
	virtual bool					IsFullscreen() override;
	virtual bool					IsMinimised() override;
	virtual bool					IsForeground() override;
	virtual void					EnableScrollBars(bool Horz,bool Vert) override;

	ESContext											mESContext;
	void													Render( std::function<void()> Frame );
};

Platform::TWindow::TWindow(const std::string& Name)
{
	esInitContext( &mESContext );

	// tsdk: the width and height are set to the size of the screen in this function, leaving them in here in case that needs to change in future
	esCreateWindow( &mESContext, Name.c_str(), 0, 0, ES_WINDOW_ALPHA );
}

std::shared_ptr<SoyWindow> Platform::CreateWindow(const std::string& Name,Soy::Rectx<int32_t>& Rect,bool Resizable)
{
	std::shared_ptr<SoyWindow> Window;

	Window.reset( new Platform::TWindow( Name ) );

	return Window;
}

Soy::Rectx<int32_t> Platform::TWindow::GetScreenRect()
{
	Soy_AssertTodo();
}

void Platform::TWindow::Render( std::function<void()> Frame )
{
	esRegisterDrawFunc( &mESContext, Frame );

	esMainLoop( &mESContext );
}