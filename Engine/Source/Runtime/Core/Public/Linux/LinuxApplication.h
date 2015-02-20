// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericApplication.h"
#include "LinuxWindow.h"

typedef SDL_GameController* SDL_HController;

class FLinuxWindow;
class FGenericApplicationMessageHandler;


class FLinuxApplication : public GenericApplication
{

public:
	static FLinuxApplication* CreateLinuxApplication();

	enum UserDefinedEvents
	{
		CheckForDeactivation
	};

public:	
	virtual ~FLinuxApplication();
	
	virtual void DestroyApplication() override;

public:
	virtual void SetMessageHandler( const TSharedRef< class FGenericApplicationMessageHandler >& InMessageHandler ) override;

	virtual void PollGameDeviceState( const float TimeDelta ) override;

	virtual void PumpMessages( const float TimeDelta ) override;

	virtual void ProcessDeferredEvents( const float TimeDelta ) override;

	virtual TSharedRef< FGenericWindow > MakeWindow() override;

	virtual void InitializeWindow( const TSharedRef< FGenericWindow >& Window, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FGenericWindow >& InParent, const bool bShowImmediately ) override;

	virtual void SetCapture( const TSharedPtr< FGenericWindow >& InWindow ) override;

	virtual void* GetCapture( void ) const override;

	virtual void SetHighPrecisionMouseMode( const bool Enable, const TSharedPtr< FGenericWindow >& InWindow ) override;

	virtual bool IsUsingHighPrecisionMouseMode() const override { return bUsingHighPrecisionMouseInput; }
	//	
	virtual FModifierKeysState GetModifierKeys() const override;
	//	
	virtual FPlatformRect GetWorkArea( const FPlatformRect& CurrentWindow ) const override;
	//	X
	virtual bool TryCalculatePopupWindowPosition( const FPlatformRect& InAnchor, const FVector2D& InSize, const EPopUpOrientation::Type Orientation, /*OUT*/ FVector2D* const CalculatedPopUpPosition ) const override;

	virtual EWindowTransparency GetWindowTransparencySupport() const override
	{
		return EWindowTransparency::PerWindow;
	}

	void AddPendingEvent( SDL_Event event );

	void OnMouseCursorLock( bool bLockEnabled );

	void RemoveEventWindow(SDL_HWindow Window);

	void RemoveRevertFocusWindow(SDL_HWindow HWnd);

	EWindowZone::Type WindowHitTest( const TSharedPtr< FLinuxWindow > &window, int x, int y );

	TSharedPtr< FLinuxWindow > FindWindowBySDLWindow( SDL_Window *win );

	virtual bool IsCursorDirectlyOverSlateWindow() const override;

	void SendDestroyEvent(SDL_Window * HWnd);

private:

	FLinuxApplication();

	TCHAR ConvertChar( SDL_Keysym Keysym );

	TSharedPtr< FLinuxWindow > FindEventWindow( SDL_Event *pEvent );

	void UpdateMouseCaptureWindow( SDL_HWindow TargetWindow );

	void ProcessDeferredMessage( SDL_Event Event );

	/** 
	 * Determines whether this particular SDL_KEYDOWN event should also be routed to OnKeyChar()
	 *
	 * @param KeyDownEvent event in question
	 *
	 * @return true if character needs to be passed to OnKeyChar
	 */
	static bool GeneratesKeyCharMessage(const SDL_KeyboardEvent & KeyDownEvent);

private:

	struct SDLControllerState
	{
		SDL_HController controller;

		// We need to remember if the "button" was previously pressed so we don't generate extra events
		bool analogOverThreshold[10];
	};

	bool bUsingHighPrecisionMouseInput;

	TArray< SDL_Event > PendingEvents;

	TArray< TSharedRef< FLinuxWindow > > Windows;

	/** Array of windows to focus when current gets removed. */
	TArray< TSharedRef< FLinuxWindow > > RevertFocusStack;

	int32 bAllowedToDeferMessageProcessing;

	bool bIsMouseCursorLocked;
	bool bIsMouseCaptureEnabled;

	/** Window that we think has been activated last. */
	TSharedPtr< FLinuxWindow > CurrentlyActiveWindow;

	/** Window that we think has been previously active. */
	TSharedPtr< FLinuxWindow > PreviousActiveWindow;

	SDL_HWindow MouseCaptureWindow;

	SDLControllerState *ControllerStates;

	float fMouseWheelScrollAccel;

	/** List of input devices implemented in external modules. */
	TArray< TSharedPtr<class IInputDevice> > ExternalInputDevices;

	/** Whether input plugins have been loaded */
	bool bHasLoadedInputPlugins;

	/** Whether we entered one of our own windows */
	bool bInsideOwnWindow;

	/** This is used to assist the drag/drop on tabs. */
	bool bIsDragWindowButtonPressed;

	/** Used to check if the application is active or not. */
	bool bActivateApp;
};


extern FLinuxApplication* LinuxApplication;
