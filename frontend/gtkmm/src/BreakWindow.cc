// BreakWindow.cc --- base class for the break windows
//
// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008 Rob Caelers & Raymond Penners
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

static const char rcsid[] = "$Id$";

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctime>

// FIXME: Debug code, remove later
#ifdef PLATFORM_OS_WIN32
#include "w32debug.hh"
#endif

#include "preinclude.h"
#include "debug.hh"
#include "nls.h"

#include <gtkmm/window.h>
#include <gtkmm/stock.h>
#include <gtkmm/buttonbox.h>
#include <gtkmm/button.h>

#include <math.h>

#include "GUI.hh"
#include "BreakWindow.hh"
#include "IBreakResponse.hh"
#include "GtkUtil.hh"
#include "WindowHints.hh"
#include "Frame.hh"
#include "System.hh"
#include "Util.hh"
#include "ICore.hh"
#include "IConfigurator.hh"
#include "CoreFactory.hh"

#if defined(PLATFORM_OS_WIN32)
#include "DesktopWindow.hh"
#elif defined(PLATFORM_OS_UNIX)
#include "desktop-window.h"
#endif

using namespace workrave;

//! Constructor
/*!
 *  \param control The controller.
 */
BreakWindow::BreakWindow(BreakId break_id, HeadInfo &head,
                         BreakFlags break_flags,
                         GUIConfig::BlockMode mode) :
#ifdef PLATFORM_OS_WIN32
/*
 Windows will have a gtk toplevel window regardless of mode.
 Hopefully this takes care of the phantom parent problem.
 Also, the break window title now appears on the taskbar, and
 it will show up in Windows Task Manager's application list.
*/
         Gtk::Window( Gtk::WINDOW_TOPLEVEL ),
#else
         Gtk::Window(mode==GUIConfig::BLOCK_MODE_NONE
                     ? Gtk::WINDOW_TOPLEVEL
                     : Gtk::WINDOW_POPUP),
#endif
         block_mode(mode),
         break_flags(break_flags),
         frame(NULL),
         break_response(NULL),
         gui(NULL),
         visible(false)
{
  this->break_id = break_id;

#ifdef PLATFORM_OS_WIN32
  desktop_window = NULL;

  if (mode != GUIConfig::BLOCK_MODE_NONE)
  {
    // Disable titlebar to appear like a popup
    set_decorated(false);
    set_skip_taskbar_hint(true);
  }
#endif

#ifdef PLATFORM_OS_UNIX
  GtkUtil::set_wmclass(*this, "Break");
#endif

  // On W32, must be *before* realize, otherwise a border is drawn.
  set_resizable(false);

  // Need to realize window before it is shown
  // Otherwise, there is not gobj()...
  realize();
  if (mode == GUIConfig::BLOCK_MODE_NONE)
    {
      Glib::RefPtr<Gdk::Window> window = get_window();
      window->set_functions(Gdk::FUNC_MOVE);
    }

  // trace window handles:
  // FIXME: debug, remove later
#ifdef PLATFORM_OS_WIN32
  HWND _hwnd = (HWND) GDK_WINDOW_HWND( Gtk::Widget::gobj()->window );
  HWND _scope = (HWND) GDK_WINDOW_HWND( GTK_WIDGET( this->gobj() )->window );
  HWND _hRoot = GetAncestor( _hwnd, GA_ROOT );
  HWND _hParent = GetAncestor( _hwnd, GA_PARENT );
  HWND _hDesktop = GetDesktopWindow();
  
  APPEND_TIME( "BreakWindow created", hex << _hwnd );
  
  if( _hwnd != _scope )
    {
      APPEND( "!!!!!!!!!!!!!!!", "Scope issue: " << hex << _scope );
      APPEND_ENDL();
	}
  
  if( _hwnd != _hRoot )
    {
	  APPEND( "GetDesktopWindow()", hex << _hDesktop );
      APPEND( "!!!!!!!!!!!!!!!", "BreakWindow GA_ROOT: " << hex << _hRoot );
      APPEND_ENDL();
	}
  
  if( _hParent != _hDesktop )
    {
	  APPEND( "GetDesktopWindow()", hex << _hDesktop );
      APPEND( "!!!!!!!!!!!!!!!", "PreludeWindow GA_PARENT: " << hex << _hParent );
      
      HWND _hTemp;
      while( IsWindow( _hParent ) && _hParent != _hDesktop )
        {
		  _hTemp = _hParent;
          _hParent = GetAncestor( _hTemp, GA_PARENT );
		  HWND _hParent2 = (HWND)GetWindowLong( _hTemp, GWL_HWNDPARENT );
          if( _hParent == _hTemp )
              break;
          APPEND( "!!!!!!!!!!!!!!!", hex << _hTemp << " GA_PARENT: " << hex << _hParent );
          APPEND( "!!!!!!!!!!!!!!!", hex << _hTemp << " GWL_HWNDPARENT: " << hex << _hParent2 );
        }
      APPEND_ENDL();
    }
    
#endif

  this->head = head;
  if (head.valid)
    {
      Gtk::Window::set_screen(head.screen);
    }

  bool initial_ignore_activity = false;
#ifdef PLATFORM_OS_WIN32
  bool force_focus = false;
  CoreFactory::get_configurator()->get_value_with_default( "advanced/force_focus",
                                                      force_focus,
                                                      false);
  if (force_focus)
    {
      initial_ignore_activity = true;
    }

#endif

  ICore *core = CoreFactory::get_core();
  assert(core != NULL);
  core->set_insist_policy(initial_ignore_activity ?
                          ICore::INSIST_POLICY_IGNORE :
                          ICore::INSIST_POLICY_HALT);
}


//! Init GUI
void
BreakWindow::init_gui()
{
  if (gui == NULL)
    {
      gui = manage(create_gui());

      if (block_mode == GUIConfig::BLOCK_MODE_NONE)
        {
          set_border_width(12);
          add(*gui);
        }
      else
        {
          set_border_width(0);
          Frame *window_frame = manage(new Frame());
          window_frame->set_border_width(0);
          window_frame->set_frame_style(Frame::STYLE_BREAK_WINDOW);

          frame = manage(new Frame());
          frame->set_frame_style(Frame::STYLE_SOLID);
          frame->set_frame_width(6);
          frame->set_border_width(6);
          frame->set_frame_flashing(0);
          frame->set_frame_visible(false);

          window_frame->add(*frame);
          frame->add(*gui);

          if (block_mode == GUIConfig::BLOCK_MODE_ALL)
            {
#ifdef PLATFORM_OS_WIN32
              desktop_window = new DesktopWindow(head);
              add(*window_frame);
#elif defined(PLATFORM_OS_UNIX)
              set_size_request(head.get_width(),
                               head.get_height());
              set_app_paintable(true);
              set_desktop_background(GTK_WIDGET (gobj())->window);
              Gtk::Alignment *align
                = manage(new Gtk::Alignment(0.5, 0.5, 0.0, 0.0));
              align->add(*window_frame);
              add(*align);
#endif
            }
          else
            {
              add(*window_frame);
            }
        }
      show_all_children();
      stick();

      // Set window hints.
      WindowHints::set_skip_winlist(this, true);
      // causes windows to display on win32
      // WindowHints::set_always_on_top(this, true);

      // FIXME: check if it was intentionally not unset for RB
      if (break_id != BREAK_ID_REST_BREAK)
        {
          unset_flags(Gtk::CAN_FOCUS);
        }
    }
}



//! Destructor.
BreakWindow::~BreakWindow()
{
  TRACE_ENTER("BreakWindow::~BreakWindow");

  if (frame != NULL)
    {
      frame->set_frame_flashing(0);
    }

#ifdef PLATFORM_OS_WIN32
  delete desktop_window;
#endif
  TRACE_EXIT();
}


//! Centers the window.
void
BreakWindow::center()
{
  GtkUtil::center_window(*this, head);
}



//! Creates the lock button
Gtk::Button *
BreakWindow::create_lock_button()
{
  Gtk::Button *ret;
  if (System::is_lockable())
    {
      ret = manage(GtkUtil::create_image_button(_("Lock"), "lock.png"));
      ret->signal_clicked()
        .connect(sigc::mem_fun(*this, &BreakWindow::on_lock_button_clicked));
      GTK_WIDGET_UNSET_FLAGS(ret->gobj(), GTK_CAN_FOCUS);
    }
  else
    {
      ret = NULL;
    }
  return ret;
}

//! Creates the lock button
Gtk::Button *
BreakWindow::create_shutdown_button()
{
  Gtk::Button *ret;
  if (System::is_shutdown_supported())
    {
      ret = manage(GtkUtil::create_image_button(_("Shut down"), "shutdown.png"));
      ret->signal_clicked()
        .connect(sigc::mem_fun(*this, &BreakWindow::on_shutdown_button_clicked));
      GTK_WIDGET_UNSET_FLAGS(ret->gobj(), GTK_CAN_FOCUS);
    }
  else
    {
      ret = NULL;
    }
  return ret;
}

//! Creates the skip button.
Gtk::Button *
BreakWindow::create_skip_button()
{
  Gtk::Button *ret;
  ret = manage(GtkUtil::create_custom_stock_button(_("Skip"), Gtk::Stock::CLOSE));
  ret->signal_clicked()
    .connect(sigc::mem_fun(*this, &BreakWindow::on_skip_button_clicked));
  GTK_WIDGET_UNSET_FLAGS(ret->gobj(), GTK_CAN_FOCUS);
  return ret;
}


//! Creates the postpone button.
Gtk::Button *
BreakWindow::create_postpone_button()
{
  Gtk::Button *ret;
  ret = manage(GtkUtil::create_custom_stock_button(_("Postpone"), Gtk::Stock::REDO));
  ret->signal_clicked()
    .connect(sigc::mem_fun(*this, &BreakWindow::on_postpone_button_clicked));
  GTK_WIDGET_UNSET_FLAGS(ret->gobj(), GTK_CAN_FOCUS);
  return ret;
}



//! The lock button was clicked.
void
BreakWindow::on_lock_button_clicked()
{
  if (System::is_lockable())
    {
      GUI *gui = GUI::get_instance();
      assert(gui != NULL);

      gui->interrupt_grab();
      System::lock();
    }
}

//! The lock button was clicked.
void
BreakWindow::on_shutdown_button_clicked()
{
#ifdef HAVE_GNOME
  GUI *gui = GUI::get_instance();
  assert(gui != NULL);
  gui->interrupt_grab();
#endif
  System::shutdown();
}


//! User has closed the main window.
bool
BreakWindow::on_delete_event(GdkEventAny *)
{
  on_postpone_button_clicked();
  return TRUE;
}


//! Break response
inline void
BreakWindow::set_response(IBreakResponse *bri)
{
  break_response = bri;
}


//! The postpone button was clicked.
void
BreakWindow::on_postpone_button_clicked()
{
  TRACE_ENTER("BreakWindow::on_postpone_button_clicked");
  if (break_response != NULL)
    {
      break_response->postpone_break(break_id);
    }
  TRACE_EXIT();
}



//! The skip button was clicked.
void
BreakWindow::on_skip_button_clicked()
{
  if (break_response != NULL)
    {
      break_response->skip_break(break_id);
    }
}



//! Control buttons.
Gtk::HButtonBox *
BreakWindow::create_break_buttons(bool lockable,
                                  bool shutdownable)
{
  Gtk::HButtonBox *box = NULL;

  if ((break_flags != BREAK_FLAGS_NONE) || lockable || shutdownable)
    {
      box = new Gtk::HButtonBox(Gtk::BUTTONBOX_END, 6);

      Gtk::Button *shutdown_button = NULL;
      if (shutdownable)
        {
          shutdown_button = create_shutdown_button();
          if (shutdown_button != NULL)
            {
              box->pack_end(*shutdown_button, Gtk::SHRINK, 0);
            }
        }

      if (lockable)
        {
          Gtk::Button *lock_button = create_lock_button();
          if (lock_button != NULL)
            {
              box->pack_end(*lock_button, Gtk::SHRINK, 0);
            }
        }

      if ((break_flags & BREAK_FLAGS_SKIPPABLE) != 0)
        {
          Gtk::Button *skip_button = create_skip_button();
          box->pack_end(*skip_button, Gtk::SHRINK, 0);
        }
      
        if ((break_flags & BREAK_FLAGS_POSTPONABLE) != 0)
        {
          Gtk::Button *postpone_button = create_postpone_button();
          box->pack_end(*postpone_button, Gtk::SHRINK, 0);
        }
    }

  return box;
}


//! Starts the daily limit.
void
BreakWindow::init()
{
  TRACE_ENTER("BreakWindow::init");
  init_gui();
  TRACE_EXIT();
}

//! Starts the daily limit.
void
BreakWindow::start()
{
  TRACE_ENTER("BreakWindow::start");
 
  update_break_window();
  center();
#ifdef PLATFORM_OS_WIN32
  if (desktop_window)
    desktop_window->set_visible(true);
#endif
  show_all();

  // Set window hints.
  WindowHints::set_skip_winlist(this, true);
  WindowHints::set_always_on_top(this, true);
  raise();

  // In case the show_all resized the window...
  center();

 
  TRACE_EXIT();
}

//! Stops the daily limit.
void
BreakWindow::stop()
{
  TRACE_ENTER("BreakWindow::stop");

  if (frame != NULL)
    {
      frame->set_frame_flashing(0);
    }

  hide_all();
  visible = false;
  
#ifdef PLATFORM_OS_WIN32
  if (desktop_window)
    desktop_window->set_visible(false);
#endif

  TRACE_EXIT();
}


//! Self-Destruct
/*!
 *  This method MUST be used to destroy the objects through the
 *  IBreakWindow. it is NOT possible to do a delete on
 *  this interface...
 */
void
BreakWindow::destroy()
{
  delete this;
}

//! Refresh
void
BreakWindow::refresh()
{
  update_break_window();
  
//   if (!visible)
//     {
//       center();
// #ifdef PLATFORM_OS_WIN32
//       if (desktop_window)
//         desktop_window->set_visible(true);
// #endif
//       show_all();

//       // Set window hints.
//       WindowHints::set_skip_winlist(this, true);
//       WindowHints::set_always_on_top(this, true);
//       raise();

//       // In case the show_all resized the window...
//       center();

//       visible = true;
//     }
  
#ifdef PLATFORM_OS_WIN32
  if (block_mode != GUIConfig::BLOCK_MODE_NONE)
    {
      WindowHints::set_always_on_top(this, true);
    }
#endif
}

Glib::RefPtr<Gdk::Window>
BreakWindow::get_gdk_window()
{
  return get_window();
}



void
BreakWindow::update_break_window()
{
}
