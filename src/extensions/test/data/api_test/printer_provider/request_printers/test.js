// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function eitherError(error1, error2) {
  return new RegExp(error1 + '|' + error2);
}

chrome.test.sendMessage('loaded', function(test) {
  chrome.test.runTests([function printTest() {
    if (test == 'NO_LISTENER') {
      chrome.test.sendMessage('ready');
      chrome.test.succeed();
      return;
    }

    chrome.printerProvider.onGetPrintersRequested.addListener(
        function(callback) {
          chrome.test.assertFalse(!!chrome.printerProviderInternal);
          chrome.test.assertTrue(!!callback);

          if (test == 'ASYNC_RESPONSE') {
            setTimeout(callback.bind(null, [{
              id: 'printer1',
              name: 'Printer 1',
              description: 'Test printer',
            }]), 0);
            chrome.test.succeed();
            return;
          }

          if (test == 'IGNORE_CALLBACK') {
            chrome.test.succeed();
            return;
          }

          if (test == 'NOT_ARRAY') {
            var jsBindingsError =
                'Invalid value for argument 1. ' +
                'Expected \'array\' but got \'string\'.'
            var nativeBindingsError = 'No matching signature';
            chrome.test.assertThrows(
                callback, ['XXX'],
                eitherError(jsBindingsError, nativeBindingsError));
          } else if (test == 'INVALID_PRINTER_TYPE') {
            var jsBindingsError =
                'Invalid value for argument 1. ' +
                'Property \'.1\': Expected \'object\' but got \'string\'.';
            var nativeBindingsError =
                'Error at parameter \'printerInfo\': Error at index 1: ' +
                'Invalid type: expected printerProvider.PrinterInfo, ' +
                'found string.';
            chrome.test.assertThrows(
                callback,
                [[{
                  id: 'printer1',
                  name: 'Printer 1',
                  description: 'Test printer'
                }, 'printer2']],
                eitherError(jsBindingsError, nativeBindingsError));
          } else if (test == 'INVALID_PRINTER') {
            var jsBindingsError =
                'Invalid value for argument 1. ' +
                'Property \'.0.unsupported\': Unexpected property.';
            var nativeBindingsError =
                'Error at parameter \'printerInfo\': ' +
                'Error at index 0: Unexpected property: \'unsupported\'.';
            chrome.test.assertThrows(
                callback,
                [[{
                  id: 'printer1',
                  name: 'Printer 1',
                  description: 'Test printer',
                  unsupported: 'print'
                }]],
                eitherError(jsBindingsError, nativeBindingsError));
          } else {
            chrome.test.assertEq('OK', test);
            callback([{
              id: 'printer1',
              name: 'Printer 1',
              description: 'Test printer',
            }, {
              id: 'printerNoDesc',
              name: 'Printer 2'
            }]);
          }

          chrome.test.assertThrows(
              callback,
              [],
              'Event callback must not be called more than once.');

          chrome.test.succeed();
        });

    chrome.test.sendMessage('ready');
  }]);
});
