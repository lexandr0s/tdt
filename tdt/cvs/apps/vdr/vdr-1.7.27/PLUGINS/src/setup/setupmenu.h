/*********************************************************
 * DESCRIPTION: 
 *             Header File
 *
 * $Id: setupmenu.h,v 1.15 2005/10/12 13:44:14 ralf Exp $
 *
 * Contact:    ranga@vdrtools.de
 *
 * Copyright (C) 2004 by Ralf Dotzert 
 *********************************************************/

#ifndef SETUPMENU_H
#define SETUPMENU_H
#include <vdr/menu.h>
#include <vdr/interface.h>
#include <vdr/submenu.h>
#include "config.h"
/**
@author Ralf Dotzert
*/
class cSetupPluginParameter : public cOsdMenu
{
  private:
    Plugin  *_plugin;
    char     _editParameter[500];
    bool     _edit;
  public:
    cSetupPluginParameter(const char *title, Plugin *plugin);
    ~cSetupPluginParameter();
    void  Set();
    eOSState ProcessKey(eKeys Key);
};



class cSetupPluginMenu : public cOsdMenu
{
private:
    Config  *_config;
    Plugins *_plugins;
    int      _startIndex;
    bool     _moveMode;
    bool     _sortMode;
public:
    cSetupPluginMenu(Config  *config);
    ~cSetupPluginMenu();
    void  Set();
    eOSState ProcessKey(eKeys Key);
    void setHelp();
};



class cSetupVdrMenu : public cOsdMenu
{
private:
    enum MenuState { UNDEFINED, MOVE, CREATE, CREATECMD, EDIT, DELETE};
    cSubMenu  _vdrSubMenu;
    char      *createMenuName(cSubMenuNode *node);
    void      setHelp();
    MenuState _menuState;

    int       _startIndex;
    int       _createEditNodeIndex;

    char      _editTitle[50];
    char      _editCommand[999];
    int       _hasToConfirm;
    int       _isThread;
public:
    cSetupVdrMenu(const char *title);

    ~cSetupVdrMenu();
    void  Set();
    eOSState ProcessKey(eKeys Key);
};


class cSetupMenu : public cOsdMenu
{
private:

   int  _number;
   bool _error;
   bool _childLock;
   char *_childLockEntered;
   char *_childLockEnteredHidden;

protected:
   Config *_config;
public:
    cSetupMenu();
    ~cSetupMenu();
    void Set();
    void SetAskChildCode();
    eOSState ProcessKey(eKeys Key);
    eOSState StandardProcessKey(eKeys Key);
    eOSState GetCodeProcessKey(eKeys Key);
    char *GetLongHelp(const char* help);
};

class cSetupGenericMenu : public cSetupMenu
{
  private:
    MenuNode *_node;
    bool      _editItem;
    int       _currLine;
    Config   *_config;
    const char * nohk(const char *str);
  public:
    cSetupGenericMenu(const char *title, MenuNode *node, Config  *config);
    ~cSetupGenericMenu();
    void      Set();
    eOSState  ProcessKey(eKeys Key);
    void      ExecuteCommand(const char* cmd);
};

#endif
