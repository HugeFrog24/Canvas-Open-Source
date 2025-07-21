package com.tgc.sky.ui;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.RelativeLayout;

import androidx.appcompat.widget.AppCompatEditText;
import androidx.core.view.ViewCompat;

import com.tgc.sky.GameActivity;
import com.tgc.sky.SystemUI_android;
import com.tgc.sky.util.IdCounter;

import java.nio.charset.StandardCharsets;
import java.util.Objects;


public class TextField {
    public static final int kInvalidTextFieldId = -1;
    private GameActivity m_activity;
    private int m_imeOptions;
    private int m_inputType;
    private boolean m_isCallbackTextfield;
    private EditText m_textField;
    private SystemUI_android m_systemUI;
    private TextFieldLimiter m_textFieldLimiter;
    private Rect m_hitRect = new Rect();
    private final IdCounter<Integer> m_idCounter = new IdCounter<>(-1);
    private String m_textBuffer = "";
    private int m_cursorPos = -1;
    private int m_selectPos = -1;
    private String m_textBufferProgram = "";
    private int m_cursorPosProgram = -1;
    private int m_selectPosProgram = -1;
    private boolean m_submitted = false;
    private State m_state = State.kTextFieldState_Hidden;
    private boolean m_init = false;


    public enum State {
        kTextFieldState_Hidden,
        kTextFieldState_RequestHide,
        kTextFieldState_RequestShow,
        kTextFieldState_Showing
    }

    public void initWithParams(GameActivity gameActivity) {
        this.m_activity = gameActivity;
        AppCompatEditText appCompatEditText = new AppCompatEditText(this.m_activity) {

            @Override // android.widget.TextView, android.view.View
            protected void onFocusChanged(boolean z, int i, Rect rect) {
                super.onFocusChanged(z, i, rect);
                TextField.this.m_activity.notifyEditTextFocus(z);
            }

            @Override
            protected void onSelectionChanged(int i, int i2) {
                super.onSelectionChanged(i, i2);
                if (!TextField.this.m_init || TextField.this.m_textField.getVisibility() == View.INVISIBLE) {
                    return;
                }
                TextField.this.updateCursorPos(i, i2, Objects.requireNonNull(getText()).toString());
            }
        };
        this.m_textField = appCompatEditText;
        this.m_inputType = 49153;
        this.m_imeOptions = 33554436;

        /*
        this.m_inputType = InputType.TYPE_CLASS_TEXT |
                   InputType.TYPE_TEXT_FLAG_MULTI_LINE |
                   InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;

        this.m_imeOptions = EditorInfo.IME_ACTION_DONE |
                    EditorInfo.IME_FLAG_NO_EXTRACT_UI;
        */

        appCompatEditText.addTextChangedListener(new TextWatcher() { // from class: com.tgc.sky.ui.TextField.2
            @Override // android.text.TextWatcher
            public void afterTextChanged(Editable editable) {
            }

            @Override // android.text.TextWatcher
            public void beforeTextChanged(CharSequence charSequence, int i, int i2, int i3) {
            }

            @Override // android.text.TextWatcher
            public void onTextChanged(CharSequence charSequence, int i, int i2, int i3) {
                if (!TextField.this.m_init || TextField.this.m_textField.getVisibility() == View.INVISIBLE) {
                    return;
                }
                TextField.this.updateBuffer(TextField.this.m_textField.getText().toString());
            }
        });
        this.m_textField.setTextColor(ViewCompat.MEASURED_STATE_MASK);
        this.m_textField.setTextSize(18.0f);
        this.m_textField.setFitsSystemWindows(true);
        this.m_textFieldLimiter = new TextFieldLimiter();
        resizeTextField(true, 0);
        GradientDrawable gradientDrawable = new GradientDrawable();
        this.m_textField.setBackground(gradientDrawable);
        gradientDrawable.setCornerRadius(Utils.dp2px(8.0f));
        gradientDrawable.setColor(Color.argb(0.5f, 1.0f, 1.0f, 1.0f));
        gradientDrawable.setStroke(1, Color.argb(0.8f, 1.0f, 1.0f, 1.0f));
        this.m_textField.setImeOptions(33554432);
        this.m_textField.setHintTextColor(-12303292);
        this.m_textField.setGravity(8388627);
        this.m_textField.setInputType(16385);
        this.m_textField.setVisibility(View.INVISIBLE);
        this.m_textField.setFocusable(true);
        this.m_textField.setFocusableInTouchMode(true);

        this.m_textField.setOnEditorActionListener((textView, imeAction, keyEvent) -> {
            if (imeAction == EditorInfo.IME_ACTION_SEND || imeAction == EditorInfo.IME_ACTION_UNSPECIFIED) {
                TextField.this.m_activity.onKeyboardCompleteNative(
                        textView.getText().toString(),
                        TextField.this.m_isCallbackTextfield,
                        TextField.this.m_isCallbackTextfield
                );
                git.artdeell.skymodloader.MainActivity.onKeyboardCompleteNative(textView.getText().toString());
                textView.setText("");
                TextField.this.m_submitted = true;
                return true;
            }
            return false;
        });
        this.m_textField.setFilters(new InputFilter[]{this.m_textFieldLimiter});
        this.m_activity.getBridgeView().addView(this.m_textField);
        this.m_activity.addOnKeyboardListener((z, i) -> {
            if (z) {
                TextField.this.resizeTextField(false, i);
                return;
            }
            if (!TextField.this.m_submitted) {
                TextField.this.m_activity.onKeyboardCompleteNative("", TextField.this.m_isCallbackTextfield, true);
            }
            TextField.this.hideTextField();
        });
        this.m_init = true;
    }


    private boolean isVirtualKeyboard() {
        return this.m_activity.getResources().getConfiguration().keyboard == 1;
    }

    private void showVirtualKeyboard() {
        if (isVirtualKeyboard()) {
            ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(this.m_textField, 0);
        }
    }

    public int showTextFieldWithPromptAsync(final String str, final String str2, final int i, final int i2, final boolean z) {
        int iTryActivate;
        if (getState() != State.kTextFieldState_Hidden || (iTryActivate = tryActivate()) == -1) {
            return -1;
        }
        setState(State.kTextFieldState_RequestShow);
        this.m_activity.runOnUiThread(() -> {
            showTextFieldWithPrompt(str, str2, i, i2, z);
            setState(State.kTextFieldState_Showing);
        });
        return iTryActivate;
    }

    public void showTextFieldWithPrompt(String str, String str2, int i, int i2, boolean z) {
        initBufferAndCursorPos(str2);
        this.m_isCallbackTextfield = z;
        this.m_submitted = false;
        this.m_textFieldLimiter.maxByteSize = i2;
        this.m_textFieldLimiter.maxCharacters = i;
        this.m_textField.setText(str2);
        EditText editText = this.m_textField;
        editText.setSelection(0, editText.getText().length());
        this.m_textField.setHint(str);
        this.m_textField.setAlpha(0.0f);
        this.m_textField.setInputType(this.m_inputType);
        this.m_textField.setImeOptions(this.m_imeOptions);
        this.m_textField.setVisibility(View.VISIBLE);
        this.m_textField.setEnabled(true);
        this.m_textField.setFocusable(true);
        this.m_textField.setFocusableInTouchMode(true);
        this.m_textField.requestFocus();
        resizeTextField(false, 0);
        showVirtualKeyboard();
        setState(State.kTextFieldState_Showing);
    }

    public void hideTextFieldAsync() {
        if (getState() == State.kTextFieldState_RequestHide || getState() == State.kTextFieldState_Hidden) {
            return;
        }
        setState(State.kTextFieldState_RequestHide);
        this.m_activity.runOnUiThread(this::hideTextField);
    }

    public void hideTextField() {
        if (!this.m_submitted) {
            this.m_activity.onKeyboardCompleteNative("", this.m_isCallbackTextfield, true);
        }
        if (getState() != State.kTextFieldState_Hidden) {
            ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(this.m_textField.getWindowToken(), 0);
            GameActivity.hideNavigationFullScreen(this.m_activity.getBridgeView());
            this.m_textField.setInputType(0);
            this.m_textField.setImeOptions(0);
            this.m_textField.clearFocus();
            this.m_textField.setFocusableInTouchMode(false);
            this.m_textField.setFocusable(false);
            this.m_textField.setEnabled(false);
            this.m_textField.setVisibility(View.GONE);
            this.m_activity.getBridgeView().requestFocus();
            this.m_activity.getBridgeView().requestFocusFromTouch();
            setState(State.kTextFieldState_Hidden);
        }
        clearId();
    }

    public void resizeTextField(boolean z, int i) {
        int dp2px = Utils.dp2px(8.0f);
        Rect GetSafeAreaInsets = this.m_activity.GetSafeAreaInsets();
        RelativeLayout.LayoutParams layoutParams = new RelativeLayout.LayoutParams(-1, -2);
        if (z) {
            layoutParams.addRule(10, -1);
            layoutParams.topMargin = i + dp2px;
        } else {
            layoutParams.addRule(12, -1);
            layoutParams.bottomMargin = i + dp2px;
        }
        layoutParams.leftMargin = GetSafeAreaInsets.left + dp2px;
        layoutParams.rightMargin = GetSafeAreaInsets.right + dp2px;
        this.m_textField.setLayoutParams(layoutParams);
        this.m_textField.setPadding(dp2px, dp2px, dp2px, dp2px);
        this.m_textField.setFitsSystemWindows(true);
        this.m_textField.setAlpha(1.0f);
    }

    public float getTextFieldHeight() {
        return this.m_textField.getHeight();
    }

    public Rect getHitRect() {
        this.m_textField.getHitRect(this.m_hitRect);
        return this.m_hitRect;
    }

    private int updateCursorPosUTF8(int i, String str) {
        return str.substring(0, i).getBytes(StandardCharsets.UTF_8).length;
    }

    public void updateCursorPos(int i, int i2, String str) {
        this.m_cursorPos = updateCursorPosUTF8(i, str);
        this.m_selectPos = i != i2 ? updateCursorPosUTF8(i2, str) : -1;
    }

    public void updateBuffer(String str) {
        this.m_textBuffer = str;
    }

    private void initBufferAndCursorPos(String str) {
        updateBuffer(str);
        this.m_textBufferProgram = this.m_textBuffer;
        int length = str.length();
        updateCursorPos(length, length, str);
        this.m_cursorPosProgram = this.m_cursorPos;
        this.m_selectPosProgram = this.m_selectPos;
    }

    public String getTextBuffer() {
        return this.m_textBuffer;
    }

    public String getPrompt() {
        return this.m_textField.getHint().toString();
    }

    public int getCursorPos() {
        return this.m_cursorPos;
    }

    public int getSelectPos() {
        return this.m_selectPos;
    }

    public int tryActivate() {
        int iIntValue;
        synchronized (this) {
            iIntValue = ((Integer) this.m_idCounter.TryActivate()).intValue();
        }
        return iIntValue;
    }

    public boolean isIdActive(int i) {
        boolean zIsActiveId;
        synchronized (this) {
            zIsActiveId = this.m_idCounter.IsActiveId(Integer.valueOf(i));
        }
        return zIsActiveId;
    }

    public void clearId() {
        synchronized (this) {
            this.m_idCounter.ClearId();
        }
    }

    public State getState() {
        return this.m_state;
    }

    public void setState(State state) {
        this.m_state = state;
    }
}