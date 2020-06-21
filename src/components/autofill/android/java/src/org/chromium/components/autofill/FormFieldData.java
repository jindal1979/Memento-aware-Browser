// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.autofill;

import android.graphics.RectF;

import androidx.annotation.IntDef;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * The wrap class of native autofill::FormFieldDataAndroid.
 */
@JNINamespace("autofill")
public class FormFieldData {
    /**
     * Define the control types supported by android.view.autofill.AutofillValue.
     *
     * Android doesn't have DATALIST control, it is sent to the Autofill service as
     * View.AUTOFILL_TYPE_TEXT with AutofillOptions.
     */
    @IntDef({ControlType.TEXT, ControlType.TOGGLE, ControlType.LIST, ControlType.DATALIST})
    @Retention(RetentionPolicy.SOURCE)
    public @interface ControlType {
        int TEXT = 0;
        int TOGGLE = 1;
        int LIST = 2;
        int DATALIST = 3;
    }

    public final String mLabel;
    public final String mName;
    public final String mAutocompleteAttr;
    public final boolean mShouldAutocomplete;
    public final String mPlaceholder;
    public final String mType;
    public final String mId;
    public final String[] mOptionValues;
    public final String[] mOptionContents;
    public final @ControlType int mControlType;
    public final int mMaxLength;
    public final String mHeuristicType;
    public final String[] mDatalistValues;
    public final String[] mDatalistLabels;

    // The bounds in the viewport's coordinates
    private final RectF mBounds;
    // The bounds in the container view's coordinates.
    private RectF mBoundsInContainerViewCoordinates;

    private boolean mIsChecked;
    private String mValue;
    // Indicates whether mValue is autofilled.
    private boolean mAutofilled;
    // Indicates whether this fields was autofilled, but changed by user.
    private boolean mPreviouslyAutofilled;

    private FormFieldData(String name, String label, String value, String autocompleteAttr,
            boolean shouldAutocomplete, String placeholder, String type, String id,
            String[] optionValues, String[] optionContents, boolean isCheckField, boolean isChecked,
            int maxLength, String heuristicType, float left, float top, float right, float bottom,
            String[] datalistValues, String[] datalistLabels) {
        mName = name;
        mLabel = label;
        mValue = value;
        mAutocompleteAttr = autocompleteAttr;
        mShouldAutocomplete = shouldAutocomplete;
        mPlaceholder = placeholder;
        mType = type;
        mId = id;
        mOptionValues = optionValues;
        mOptionContents = optionContents;
        mIsChecked = isChecked;
        mDatalistLabels = datalistLabels;
        mDatalistValues = datalistValues;
        if (mOptionValues != null && mOptionValues.length != 0) {
            mControlType = ControlType.LIST;
        } else if (mDatalistValues != null && mDatalistValues.length != 0) {
            mControlType = ControlType.DATALIST;
        } else if (isCheckField) {
            mControlType = ControlType.TOGGLE;
        } else {
            mControlType = ControlType.TEXT;
        }
        mMaxLength = maxLength;
        mHeuristicType = heuristicType;
        mBounds = new RectF(left, top, right, bottom);
    }

    public @ControlType int getControlType() {
        return mControlType;
    }

    public RectF getBounds() {
        return mBounds;
    }

    public void setBoundsInContainerViewCoordinates(RectF bounds) {
        mBoundsInContainerViewCoordinates = bounds;
    }

    public RectF getBoundsInContainerViewCoordinates() {
        return mBoundsInContainerViewCoordinates;
    }

    /**
     * @return value of field.
     */
    @CalledByNative
    public String getValue() {
        return mValue;
    }

    public void setAutofillValue(String value) {
        mValue = value;
        updateAutofillState(true);
    }

    public void setChecked(boolean checked) {
        mIsChecked = checked;
        updateAutofillState(true);
    }

    @CalledByNative
    private void updateValue(String value) {
        mValue = value;
        updateAutofillState(false);
    }

    @CalledByNative
    public boolean isChecked() {
        return mIsChecked;
    }

    public boolean hasPreviouslyAutofilled() {
        return mPreviouslyAutofilled;
    }

    private void updateAutofillState(boolean autofilled) {
        if (mAutofilled && !autofilled) mPreviouslyAutofilled = true;
        mAutofilled = autofilled;
    }

    @CalledByNative
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public static FormFieldData createFormFieldData(String name, String label, String value,
            String autocompleteAttr, boolean shouldAutocomplete, String placeholder, String type,
            String id, String[] optionValues, String[] optionContents, boolean isCheckField,
            boolean isChecked, int maxLength, String heuristicType, float left, float top,
            float right, float bottom, String[] datalistValues, String[] datalistLabels) {
        return new FormFieldData(name, label, value, autocompleteAttr, shouldAutocomplete,
                placeholder, type, id, optionValues, optionContents, isCheckField, isChecked,
                maxLength, heuristicType, left, top, right, bottom, datalistValues, datalistLabels);
    }
}
