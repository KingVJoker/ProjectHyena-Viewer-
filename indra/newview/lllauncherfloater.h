#ifndef LL_LAUNCHER_FLOATER_H
#define LL_LAUNCHER_FLOATER_H

#include "llpanel.h"
#include "llsecapi.h"

class LLComboBox;
class LLLineEditor;
class LLCheckBoxCtrl;
class LLButton;
class LLTextBox;

class LLLauncherFloater : public LLPanel
{
public:
    // --- ADD THIS STRUCT ---
    struct Params : public LLPanel::Params
    {
        Params();
    };
    // -----------------------

    LLLauncherFloater(const Params& p); // <-- CHANGE THIS to take Params
    virtual ~LLLauncherFloater();

    /*virtual*/ bool postBuild();
    void             onLaunchClicked();
    void             populateFields(LLPointer<LLCredential> credential, bool remember_user, bool remember_password);
    void             getFields(LLPointer<LLCredential>& user_credential, bool& remember_user, bool& remember_password);
    void             giveFocus();

    // Multi-factor authentication screen (shown inside the launcher when the login asks for a code)
    static bool showMFA(const std::string& message); // true = the launcher screen took the challenge
    void        hideMFA();
    void        updateMfaButton(); // Verify is enabled only with exactly six digits
    void        onMfaVerify();
    void        onMfaCancel();

private:
    LLComboBox*     mAccountCombo;
    LLLineEditor*   mPasswordEdit;
    LLComboBox*     mStartLocationCombo;
    LLCheckBoxCtrl* mRememberUserCheck;
    LLCheckBoxCtrl* mRememberPassCheck;

    LLPanel*        mMfaPanel       = NULL;
    LLLineEditor*   mMfaCodeEdit    = NULL;
    LLCheckBoxCtrl* mMfaRememberCheck = NULL;
    LLButton*       mMfaVerifyBtn   = NULL;
    LLTextBox*      mMfaError       = NULL;
};

#endif // LL_LAUNCHER_FLOATER_H
