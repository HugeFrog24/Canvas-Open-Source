package com.tgc.sky.ui;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.RelativeLayout;
import android.widget.TextView;

import androidx.appcompat.widget.AppCompatEditText;
import androidx.core.view.ViewCompat;

import com.tgc.sky.GameActivity;
import com.tgc.sky.SystemUI_android;

import java.nio.charset.StandardCharsets;
import java.util.Objects;


public class TextField {
    public static final int kInvalidTextFieldId = -1;
    private GameActivity m_activity;
    private boolean m_isCallbackTextfield;
    private SystemUI_android m_systemUI;
    private EditText m_textField;
    private TextFieldLimiter m_textFieldLimiter;
    private String m_userText;
    private int m_id = -1;
    private int m_counter = -1;
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

    public void initWithParams(GameActivity gameActivity, SystemUI_android systemUI_android) {
        this.m_activity = gameActivity;
        this.m_systemUI = systemUI_android;
        AppCompatEditText appCompatEditText = new AppCompatEditText(this.m_activity) { // from class: com.tgc.sky.ui.TextField.1
            @Override // android.widget.TextView, android.view.View
            protected void onFocusChanged(boolean z, int i, Rect rect) {
                super.onFocusChanged(z, i, rect);
                TextField.this.m_activity.notifyEditTextFocus(z);
            }

            @Override // android.widget.TextView, android.view.View
            public boolean onKeyPreIme(int i, KeyEvent keyEvent) {
                if (keyEvent.getKeyCode() == 4) {
                    TextField.this.hideTextField();
                    return true;
                }
                return super.dispatchKeyEvent(keyEvent);
            }

            @Override // android.widget.TextView
            protected void onSelectionChanged(int i, int i2) {
                super.onSelectionChanged(i, i2);
                if (!TextField.this.m_init || TextField.this.m_textField.getVisibility() == View.INVISIBLE) {
                    return;
                }
                TextField.this.updateCursorPos(i, i2, getText().toString());
            }

            @Override // android.widget.TextView
            public void onTextChanged(CharSequence charSequence, int i, int i2, int i3) {
                super.onTextChanged(charSequence, i, i2, i3);
                if (!TextField.this.m_init || TextField.this.m_textField.getVisibility() == View.INVISIBLE) {
                    return;
                }
                TextField.this.updateBuffer(TextField.this.m_textField.getText().toString());
            }
        };
        this.m_textField = appCompatEditText;
        appCompatEditText.setTextColor(ViewCompat.MEASURED_STATE_MASK);
        this.m_textField.setTextSize(18.0f);
        this.m_textField.setFitsSystemWindows(false);
        this.m_textFieldLimiter = new TextFieldLimiter();
        resizeTextField(true, 0);
        GradientDrawable gradientDrawable = new GradientDrawable();
        gradientDrawable.setCornerRadius(Utils.dp2px(8.0f));
        gradientDrawable.setColor(Color.argb(0.5f, 1.0f, 1.0f, 1.0f));
        gradientDrawable.setStroke(1, Color.argb(0.8f, 1.0f, 1.0f, 1.0f));
        this.m_textField.setBackground(gradientDrawable);
        this.m_textField.setImeOptions(33554432);
        this.m_textField.setHintTextColor(-12303292);
        this.m_textField.setInputType(16385);
        this.m_textField.setVisibility(View.INVISIBLE);
        this.m_textField.setFocusable(true);
        this.m_textField.setFocusableInTouchMode(true);
        this.m_textField.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView textView, int i, KeyEvent keyEvent) {
                if (i == 0 || i == 4) {
                    TextField.this.m_activity.onKeyboardCompleteNative(TextField.this.m_userText, TextField.this.m_isCallbackTextfield, TextField.this.m_isCallbackTextfield);
                    Log.d("TextField", "onKeyboardCompleteNative: " + TextField.this.m_userText);
                    textView.setText("");
                    TextField.this.m_submitted = true;
                    return true;
                }
                return false;
            }
        });
        this.m_textField.setFilters(new InputFilter[]{this.m_textFieldLimiter});
        this.m_activity.getBrigeView().addView(this.m_textField);
        this.m_activity.addOnKeyboardListener(new GameActivity.OnKeyboardListener() {
            @Override
            public void onKeyboardChange(boolean z, int i) {
                if (z) {
                    TextField.this.resizeTextField(false, i);
                } else {
                    TextField.this.hideTextField();
                }
            }
        });
        this.m_init = true;
    }


    public void showTextFieldWithPrompt(String str, int i, int i2) {
        showTextFieldWithPrompt(str, "", i, i2, false);
    }

    public void showTextFieldWithPrompt(String str, String str2, int i, int i2, boolean z) {
        initBufferAndCursorPos(str2);
        this.m_isCallbackTextfield = z;
        this.m_submitted = false;
        this.m_textFieldLimiter.maxByteSize = i2;
        this.m_textFieldLimiter.maxCharacters = i;
        this.m_textField.setText(str2);
        EditText editText = this.m_textField;
        editText.setSelection(editText.length());
        this.m_textField.setHint(str);
        EditText editText2 = this.m_textField;
        editText2.setImeOptions(editText2.getImeOptions() | 4);
        this.m_textField.setAlpha(0.0f);
        this.m_textField.setVisibility(View.VISIBLE);
        this.m_textField.requestFocus();
        ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(this.m_textField, 0);
        setState(State.kTextFieldState_Showing);
    }

    public void hideTextField() {
        if (!this.m_submitted) {
            this.m_activity.onKeyboardCompleteNative("", this.m_isCallbackTextfield, true);
        }
        ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(this.m_textField.getWindowToken(), 0);
        GameActivity.hideNavigationFullScreen(this.m_activity.getBrigeView());
        this.m_textField.setVisibility(View.INVISIBLE);
        this.m_textField.clearFocus();
        setState(State.kTextFieldState_Hidden);
        clearId();
    }

    public void resizeTextField(boolean z, int i) {
        int dp2px = Utils.dp2px(8.0f);
        RelativeLayout.LayoutParams layoutParams = new RelativeLayout.LayoutParams(-1, Utils.dp2px(40.0f));
        if (z) {
            layoutParams.addRule(10, -1);
            layoutParams.topMargin = i + dp2px;
        } else {
            layoutParams.addRule(12, -1);
            layoutParams.bottomMargin = i + dp2px;
        }
        Rect GetSafeAreaInsets = this.m_activity.GetSafeAreaInsets();
        layoutParams.leftMargin = GetSafeAreaInsets.left + dp2px;
        layoutParams.rightMargin = GetSafeAreaInsets.right + dp2px;
        this.m_textField.setLayoutParams(layoutParams);
        this.m_textField.setPadding(dp2px, dp2px, dp2px, dp2px);
        this.m_textField.setAlpha(1.0f);
    }

    public float getTextFieldHeight() {
        return this.m_textField.getHeight();
    }

    private int updateCursorPosUTF8(int i, String str) {
        return str.substring(0, i).getBytes(StandardCharsets.UTF_8).length;
    }

    public void updateCursorPos(int i, int i2, String str) {
        this.m_cursorPos = updateCursorPosUTF8(i, str);
        this.m_selectPos = i != i2 ? updateCursorPosUTF8(i2, str) : -1;
    }

    public void updateBuffer(String str) {
        this.m_userText = str;
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
        String str = this.m_textBufferProgram;
        this.m_textBufferProgram = this.m_textBuffer;
        return str;
    }

    public int getCursorPos() {
        int i = this.m_cursorPosProgram;
        this.m_cursorPosProgram = this.m_cursorPos;
        return i;
    }

    public int getSelectPos() {
        int i = this.m_selectPosProgram;
        this.m_selectPosProgram = this.m_selectPos;
        return i;
    }

    public int TryActivate() {
        if (this.m_id != -1) {
            return -1;
        }
        int i = this.m_counter + 1;
        this.m_counter = i;
        this.m_id = i;
        return i;
    }

    public boolean IsActiveId(int i) {
        return i != -1 && i == this.m_id;
    }

    public void clearId() {
        this.m_id = -1;
    }

    public State getState() {
        return this.m_state;
    }

    public void setState(State state) {
        this.m_state = state;
    }
}
