/*
 * Copyright 2017 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Launches PaymentRequest to check whether PaymentRequest.shippingAddress is
 * the same instance as PaymentResponse.shippingAddress.
 */
function buy() { // eslint-disable-line no-unused-vars
    try {
        var details = {
            total: {
                label: 'Total',
                amount: {
                    currency: 'USD',
                    value: '5.00',
                },
            },
            shippingOptions: [{
                id: 'freeShippingOption',
                label: 'Free global shipping',
                amount: {
                    currency: 'USD',
                    value: '0',
                },
                selected: true,
            }],
        };
        var request = new PaymentRequest(
            [{
                supportedMethods: 'basic-card',
            }],
            details, {
                requestShipping: true,
            });
        request.show()
            .then(function(resp) {
                print('Same instance: ' +
                    (request.shippingAddress === resp.shippingAddress)
                    .toString());
                resp.complete('success');
            })
            .catch(function(error) {
                print('User did not authorized transaction: ' + error);
            });
    } catch (error) {
        print('Developer mistake ' + error);
    }
}
