package com.tgc.sky.ui;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.text.InputFilter;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.RelativeLayout;

import androidx.core.view.ViewCompat;

import com.tgc.sky.GameActivity;
import com.tgc.sky.SystemUI_android;

import git.artdeell.skymodloader.MainActivity;

public class TextField {
    private GameActivity m_activity;
    private boolean m_isCallbackTextfield;
    private boolean m_onKeyboardCompleteAlreadyCalled;
    private SystemUI_android m_systemUI;
    private EditText m_textField;
    private TextFieldLimiter m_textFieldLimiter;
    private String m_userText;

    public void initWithParams(GameActivity gameActivity, SystemUI_android systemUI_android) {
        this.m_activity = gameActivity;
        this.m_systemUI = systemUI_android;
        EditText editText = new EditText(this.m_activity);
        this.m_textField = editText;
        editText.setTextColor(ViewCompat.MEASURED_STATE_MASK);
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

                // Run Canvas message listeners
                MainActivity.onKeyboardCompleteNative(textView.getText().toString());
                textView.setText("");
                if (TextField.this.m_isCallbackTextfield) {
                    TextField.this.m_onKeyboardCompleteAlreadyCalled = true;
                    TextField.this.hideTextField();
                }
                return true;
            }
            return false;
        });
        this.m_textField.setFilters(new InputFilter[]{this.m_textFieldLimiter});
        this.m_activity.getBrigeView().addView(this.m_textField);

        this.m_activity.addOnKeyboardListener((z, i) -> {
            if (z) {
                TextField.this.resizeTextField(false, i);
                return;
            }
            if (!TextField.this.m_isCallbackTextfield || !TextField.this.m_onKeyboardCompleteAlreadyCalled) {
                TextField.this.m_activity.onKeyboardCompleteNative(TextField.this.m_isCallbackTextfield ? TextField.this.m_userText : "", TextField.this.m_isCallbackTextfield, true);
            }
            TextField.this.hideTextField();
        });
    }

    public void showTextFieldWithPrompt(String str, int i, int i2) {
        this.m_userText = null;
        this.m_isCallbackTextfield = false;
        this.m_onKeyboardCompleteAlreadyCalled = false;
        this.m_textFieldLimiter.maxByteSize = i2;
        this.m_textFieldLimiter.maxCharacters = i;
        this.m_textField.setText("");
        this.m_textField.setHint(str);
        EditText editText = this.m_textField;
        editText.setImeOptions(editText.getImeOptions() | 4);
        this.m_textField.setAlpha(0.0f);
        this.m_textField.setVisibility(View.VISIBLE);
        this.m_textField.requestFocus();
        ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(this.m_textField, 0);
    }

    public void showTextFieldWithPrompt(String str, String str2, int i, int i2, boolean z) {
        this.m_userText = str2;
        this.m_isCallbackTextfield = z;
        this.m_onKeyboardCompleteAlreadyCalled = false;
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
    }

    public void hideTextField() {
        ((InputMethodManager) this.m_activity.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(this.m_textField.getWindowToken(), 0);
        GameActivity.hideNavigationFullScreen(this.m_activity.getBrigeView());
        this.m_textField.setVisibility(View.INVISIBLE);
    }

    /* JADX INFO: Access modifiers changed from: private */
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

    public EditText getEditText() {
        return this.m_textField;
    }
}
