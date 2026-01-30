package com.tgc.sky.commerce;

import com.tgc.sky.GameActivity;
import java.util.List;

public interface CommerceManagerInterface {
    void Initialize(GameActivity activity, CommerceManagerCallbacks callbacks);
    void Terminate();
    boolean deliverProduct(String productId, String transactionId);
    boolean initiatePurchaseFlow(String systemProductId, String p2, String p3);
    void obtainProductInfo(List<String> productIds);
    void onResume();
    String productIdToSystemProductId(String productId);
    void queryPurchaseHistory(boolean refresh);
}
