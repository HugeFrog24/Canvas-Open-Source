package com.tgc.sky.commerce;

import com.android.billingclient.api.Purchase;
import java.util.List;

public interface CommerceManagerCallbacks {
    void onUpdateProductInfo(List<ProductInfo> productInfoList);
    void onUpdatePurchases(List<ReceiptItem> purchases, boolean p2);
    void onProductFailed(String systemProductId, String error);
    void onProductDelivered(ReceiptItem item, Purchase purchase);
}
