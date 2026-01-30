package com.tgc.sky;

import android.util.Log;
import com.tgc.sky.commerce.CommerceManager;
import com.tgc.sky.commerce.CommerceManagerCallbacks;
import com.tgc.sky.commerce.ProductInfo;
import com.tgc.sky.commerce.Receipt;
import com.tgc.sky.commerce.ReceiptItem;
import com.tgc.sky.util.HuaweiUtil;
import com.android.billingclient.api.Purchase;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import android.util.Base64;

public class SystemCommerce_android implements CommerceManagerCallbacks {

    private static final String TAG = "SystemCommerce_android";
    private static volatile SystemCommerce_android sInstance;

    private String mError;
    private final GameActivity m_activity;
    private CommerceManager m_commerceManager;
    private boolean m_hasReceipt;
    private final HashSet<String> m_pendingProductIds;
    private final HashMap<String, ProductInfo> m_productInfo;
    private boolean m_productsInitialized;
    private List<ReceiptItem> m_purchases;

    public SystemCommerce_android(GameActivity activity) {
        this.m_productInfo = new HashMap<>();
        this.m_productsInitialized = false;
        this.m_pendingProductIds = new HashSet<>();
        this.m_activity = activity;
        this.m_commerceManager = new CommerceManager();
        this.m_commerceManager.Initialize(activity, this);
        sInstance = this;
    }

    public static SystemCommerce_android getInstance() {
        return sInstance;
    }

    // AGGIUNTO per compatibilità con Canvas
    public void setActivity(GameActivity activity) {
        // L'activity è già impostata nel costruttore
    }

    public void onResume() {
        m_commerceManager.onResume();
    }

    public void onDestroy() {
        m_commerceManager.Terminate();
        m_commerceManager = null;
    }

    public boolean CanMakePayments() {
        return m_productsInitialized;
    }

    public void LoadProducts(String[] productIds) {
        List<String> list = new ArrayList<>(Arrays.asList(productIds));
        list.replaceAll(productId -> m_commerceManager.productIdToSystemProductId(productId));
        m_commerceManager.obtainProductInfo(list);
    }

    public ProductInfo GetProductInfo(String productId) {
        if (m_productsInitialized) {
            String systemProductId = m_commerceManager.productIdToSystemProductId(productId);
            return m_productInfo.get(systemProductId);
        }
        return null;
    }

    public int GetPlatformInt() {
        return HuaweiUtil.getPlatformInt();
    }

    public boolean IsPurchasePending(String productId) {
        String systemProductId = m_commerceManager.productIdToSystemProductId(productId);
        if (m_purchases != null) {
            for (ReceiptItem item : m_purchases) {
                if (item.systemProductId.equalsIgnoreCase(systemProductId) && !item.wasDelivered) {
                    return true;
                }
            }
        }
        return m_pendingProductIds.contains(systemProductId);
    }

    public boolean MakePurchase(String productId, String p2, String p3) {
        String systemProductId = m_commerceManager.productIdToSystemProductId(productId);
        if (m_productInfo.containsKey(systemProductId)) {
            if (m_commerceManager.initiatePurchaseFlow(systemProductId, p2, p3)) {
                m_pendingProductIds.add(systemProductId);
                return true;
            }
        }
        return false;
    }

    public String GetErrorMessage() {
        String error = mError;
        mError = null;
        return error;
    }

    public boolean RestorePurchases() {
        m_commerceManager.queryPurchaseHistory(false);
        return true;
    }

    public Receipt GetReceipt() {
        if (!m_hasReceipt || m_purchases == null) {
            m_hasReceipt = false;
            return null;
        }

        JSONArray purchases = new JSONArray();
        JSONArray signatures = new JSONArray();
        
        for (ReceiptItem item : m_purchases) {
            purchases.put(item.info);
            signatures.put(item.signature);
        }

        try {
            JSONObject obj = new JSONObject();
            obj.put("purchases", purchases);
            obj.put("signatures", signatures);
            
            String jsonString = obj.toString();
            Receipt receipt = new Receipt();
            receipt.base64payload = Base64.encodeToString(
                jsonString.getBytes(StandardCharsets.UTF_8), 
                Base64.NO_WRAP
            );
            receipt.type = 0;
            
            if (receipt.base64payload.length() > 0xBFFF) {
                receipt.type = -1;
            }
            
            m_hasReceipt = false;
            return receipt;
        } catch (JSONException e) {
            e.printStackTrace();
            m_hasReceipt = false;
            return null;
        }
    }

    public boolean RefreshReceipt() {
        m_commerceManager.queryPurchaseHistory(true);
        return true;
    }

    public boolean FinishPurchase(String productId, String transactionId) {
        String systemProductId = m_commerceManager.productIdToSystemProductId(productId);
        return m_commerceManager.deliverProduct(systemProductId, transactionId);
    }

    @Override
    public void onUpdateProductInfo(List<ProductInfo> productInfoList) {
        m_productInfo.clear();
        for (ProductInfo info : productInfoList) {
            Log.d(TAG, "onSuccess: " + info.systemProductId);
            m_productInfo.put(info.systemProductId, info);
        }
        m_productsInitialized = true;
        m_activity.onCommerceUpdate(true, false, false);
    }

    @Override
    public void onUpdatePurchases(List<ReceiptItem> purchases, boolean p2) {
        m_purchases = purchases;
        m_hasReceipt = purchases.size() > 0;
        m_activity.onCommerceUpdate(false, true, m_hasReceipt);
    }

    @Override
    public void onProductFailed(String systemProductId, String error) {
        m_pendingProductIds.remove(systemProductId);
        mError = error;
    }

    @Override
    public void onProductDelivered(ReceiptItem item, Purchase purchase) {
        Log.d(TAG, "onProductDelivered: " + item.systemProductId);
        m_pendingProductIds.remove(item.systemProductId);
        
        if (purchase != null) {
            ProductInfo info = m_productInfo.get(item.systemProductId);
            // CORRETTO: solo 2 parametri
            SystemAnalytics_android.getInstance().OnFinishPurchase(item, info);
        }
    }
}
