package com.tgc.sky.commerce;

import android.util.Log;
import com.android.billingclient.api.AcknowledgePurchaseParams;
import com.android.billingclient.api.AcknowledgePurchaseResponseListener;
import com.android.billingclient.api.BillingClient;
import com.android.billingclient.api.BillingClientStateListener;
import com.android.billingclient.api.BillingFlowParams;
import com.android.billingclient.api.BillingResult;
import com.android.billingclient.api.ConsumeParams;
import com.android.billingclient.api.ConsumeResponseListener;
import com.android.billingclient.api.ProductDetails;
import com.android.billingclient.api.ProductDetailsResponseListener;
import com.android.billingclient.api.Purchase;
import com.android.billingclient.api.PurchasesResponseListener;
import com.android.billingclient.api.PurchasesUpdatedListener;
import com.android.billingclient.api.QueryProductDetailsParams;
import com.android.billingclient.api.QueryPurchasesParams;
import com.tgc.sky.GameActivity;
import org.json.JSONException;

import java.util.ArrayList;
import java.util.Hashtable;
import java.util.List;

public class CommerceManager implements CommerceManagerInterface, 
        BillingClientStateListener,
        ProductDetailsResponseListener,
        PurchasesUpdatedListener,
        PurchasesResponseListener {

    private static final String PREFIX = "com.tgc.sky.android";
    private static final String TAG = "CommerceManager";

    private GameActivity mActivity;
    private BillingClient mBillingClient;
    private boolean mIsServiceConnected;
    private String mPendingPurchase;
    private final Hashtable<String, ReceiptItem> mPendingPurchases;
    private List<ProductDetails> mSkuDetails;
    private CommerceManagerCallbacks mUpdateCallbacks;

    public CommerceManager() {
        this.mPendingPurchases = new Hashtable<>();
    }

    @Override
    public void Initialize(GameActivity activity, CommerceManagerCallbacks callbacks) {
        mActivity = activity;
        mUpdateCallbacks = callbacks;

        Log.d(TAG, "Creating Billing client.");
        
        mBillingClient = BillingClient.newBuilder(activity)
                .setListener(this)
                .enablePendingPurchases()
                .build();

        Log.d(TAG, "Starting setup.");
        startServiceConnection(() -> {
            // Connection callback
        });
    }

    @Override
    public void Terminate() {
        Log.d(TAG, "Destroying the manager.");
        if (mBillingClient != null && mBillingClient.isReady()) {
            mBillingClient.endConnection();
            mBillingClient = null;
        }
    }

    @Override
    public void onResume() {
        queryPurchases();
    }

    @Override
    public String productIdToSystemProductId(String productId) {
        return PREFIX + "." + productId.toLowerCase();
    }

    @Override
    public void obtainProductInfo(List<String> productIds) {
        Runnable runnable = () -> {
            List<QueryProductDetailsParams.Product> products = new ArrayList<>();
            for (String productId : productIds) {
                products.add(QueryProductDetailsParams.Product.newBuilder()
                        .setProductId(productId)
                        .setProductType(BillingClient.ProductType.INAPP)
                        .build());
            }
            
            QueryProductDetailsParams params = QueryProductDetailsParams.newBuilder()
                    .setProductList(products)
                    .build();
            
            mBillingClient.queryProductDetailsAsync(params, this);
        };
        executeServiceRequest(runnable);
    }

    @Override
    public boolean initiatePurchaseFlow(String systemProductId, String p2, String p3) {
        if (mSkuDetails == null) return false;
        
        for (ProductDetails details : mSkuDetails) {
            if (details.getProductId().equals(systemProductId)) {
                mPendingPurchase = systemProductId;
                
                Runnable runnable = () -> {
                    List<BillingFlowParams.ProductDetailsParams> productParamsList = new ArrayList<>();
                    productParamsList.add(BillingFlowParams.ProductDetailsParams.newBuilder()
                            .setProductDetails(details)
                            .build());
                    
                    BillingFlowParams flowParams = BillingFlowParams.newBuilder()
                            .setProductDetailsParamsList(productParamsList)
                            .build();
                    
                    mBillingClient.launchBillingFlow(mActivity, flowParams);
                };
                executeServiceRequest(runnable);
                return true;
            }
        }
        return false;
    }

    @Override
    public boolean deliverProduct(String systemProductId, String transactionId) {
        ReceiptItem item = mPendingPurchases.get(transactionId);
        if (item != null && !item.wasDelivered) {
            try {
                Purchase purchase = new Purchase(item.info, item.signature);
                
                if (systemProductId.contains(".nc")) {
                    // Non-consumable - acknowledge
                    AcknowledgePurchaseParams params = AcknowledgePurchaseParams.newBuilder()
                            .setPurchaseToken(purchase.getPurchaseToken())
                            .build();
                    
                    Runnable runnable = () -> {
                        mBillingClient.acknowledgePurchase(params, new AcknowledgePurchaseResponseListener() {
                            @Override
                            public void onAcknowledgePurchaseResponse(BillingResult billingResult) {
                                if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK) {
                                    item.wasDelivered = true;
                                    mUpdateCallbacks.onProductDelivered(item, purchase);
                                }
                            }
                        });
                    };
                    executeServiceRequest(runnable);
                } else {
                    // Consumable - consume
                    ConsumeParams params = ConsumeParams.newBuilder()
                            .setPurchaseToken(purchase.getPurchaseToken())
                            .build();
                    
                    Runnable runnable = () -> {
                        mBillingClient.consumeAsync(params, new ConsumeResponseListener() {
                            @Override
                            public void onConsumeResponse(BillingResult billingResult, String purchaseToken) {
                                if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK) {
                                    item.wasDelivered = true;
                                    mUpdateCallbacks.onProductDelivered(item, purchase);
                                }
                            }
                        });
                    };
                    executeServiceRequest(runnable);
                }
                return true;
            } catch (JSONException e) {
                e.printStackTrace();
            }
        }
        return false;
    }

    @Override
    public void queryPurchaseHistory(boolean refresh) {
        queryPurchases();
    }

    @Override
    public void onProductDetailsResponse(BillingResult billingResult, List<ProductDetails> productDetailsList) {
        if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK && productDetailsList != null) {
            mSkuDetails = productDetailsList;
            
            List<ProductInfo> productInfoList = new ArrayList<>();
            for (ProductDetails details : productDetailsList) {
                ProductInfo info = new ProductInfo();
                info.systemProductId = details.getProductId();
                
                ProductDetails.OneTimePurchaseOfferDetails offerDetails = details.getOneTimePurchaseOfferDetails();
                if (offerDetails != null) {
                    info.priceMicros = offerDetails.getPriceAmountMicros() / 1000000.0;
                    info.name = details.getTitle();
                    info.desc = details.getDescription();
                    info.price = offerDetails.getFormattedPrice();
                    info.currency = offerDetails.getPriceCurrencyCode();
                }
                
                productInfoList.add(info);
            }
            
            mUpdateCallbacks.onUpdateProductInfo(productInfoList);
        }
    }

    @Override
    public void onPurchasesUpdated(BillingResult billingResult, List<Purchase> purchases) {
        if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK && purchases != null) {
            for (Purchase purchase : purchases) {
                addPendingPurchase(purchase);
            }
            
            List<ReceiptItem> receiptItems = new ArrayList<>(mPendingPurchases.values());
            mUpdateCallbacks.onUpdatePurchases(receiptItems, false);
        } else if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.USER_CANCELED) {
            // User cancelled
        } else {
            Log.w(TAG, "onPurchasesUpdated() got unknown resultCode: " + billingResult.getResponseCode());
        }
    }

    @Override
    public void onQueryPurchasesResponse(BillingResult billingResult, List<Purchase> purchases) {
        if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK && purchases != null) {
            mPendingPurchases.clear();
            for (Purchase purchase : purchases) {
                addPendingPurchase(purchase);
            }
            
            List<ReceiptItem> receiptItems = new ArrayList<>(mPendingPurchases.values());
            mUpdateCallbacks.onUpdatePurchases(receiptItems, false);
        }
    }

    @Override
    public void onBillingSetupFinished(BillingResult billingResult) {
        if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK) {
            mIsServiceConnected = true;
            queryPurchases();
        }
    }

    @Override
    public void onBillingServiceDisconnected() {
        mIsServiceConnected = false;
    }

    private void queryPurchases() {
        Runnable runnable = () -> {
            mBillingClient.queryPurchasesAsync(
                QueryPurchasesParams.newBuilder()
                    .setProductType(BillingClient.ProductType.INAPP)
                    .build(),
                this
            );
        };
        executeServiceRequest(runnable);
    }

    private void addPendingPurchase(Purchase purchase) {
        ReceiptItem item = new ReceiptItem();
        item.systemProductId = purchase.getProducts().get(0);
        item.orderId = purchase.getOrderId();
        item.quantity = purchase.getQuantity();
        item.info = purchase.getOriginalJson();
        item.signature = purchase.getSignature();
        item.wasDelivered = purchase.isAcknowledged();
        
        mPendingPurchases.put(purchase.getOrderId(), item);
    }

    private void executeServiceRequest(Runnable runnable) {
        if (mIsServiceConnected) {
            mActivity.runOnUiThread(runnable);
        } else {
            startServiceConnection(runnable);
        }
    }

    private void startServiceConnection(Runnable executeOnSuccess) {
        mActivity.runOnUiThread(() -> {
            mBillingClient.startConnection(new BillingClientStateListener() {
                @Override
                public void onBillingSetupFinished(BillingResult billingResult) {
                    if (billingResult.getResponseCode() == BillingClient.BillingResponseCode.OK) {
                        mIsServiceConnected = true;
                        if (executeOnSuccess != null) {
                            executeOnSuccess.run();
                        }
                        queryPurchases();
                    }
                }

                @Override
                public void onBillingServiceDisconnected() {
                    mIsServiceConnected = false;
                }
            });
        });
    }
}
