// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var createClassWrapper = requireNative('utils').createClassWrapper;
var schemaRegistry = requireNative('schema_registry');
var CHECK = requireNative('logging').CHECK;
var WARNING = requireNative('logging').WARNING;

/**
 * An object forEach. Calls |f| with each (key, value) pair of |obj|, using
 * |self| as the target.
 * @param {Object} obj The object to iterate over.
 * @param {function} f The function to call in each iteration.
 * @param {Object} self The object to use as |this| in each function call.
 */
function forEach(obj, f, self) {
  for (var key in obj) {
    if ($Object.hasOwnProperty(obj, key))
      $Function.call(f, self, key, obj[key]);
  }
}

/**
 * Assuming |array_of_dictionaries| is structured like this:
 * [{id: 1, ... }, {id: 2, ...}, ...], you can use
 * lookup(array_of_dictionaries, 'id', 2) to get the dictionary with id == 2.
 * @param {Array.<Object.<string, ?>>} array_of_dictionaries
 * @param {string} field
 * @param {?} value
 */
function lookup(array_of_dictionaries, field, value) {
  var filter = function (dict) {return dict[field] == value;};
  var matches = array_of_dictionaries.filter(filter);
  if (matches.length == 0) {
    return undefined;
  } else if (matches.length == 1) {
    return matches[0]
  } else {
    throw new Error("Failed lookup of field '" + field + "' with value '" +
                    value + "'");
  }
}

function loadTypeSchema(typeName, defaultSchema) {
  var parts = $String.split(typeName, '.');
  if (parts.length == 1) {
    if (defaultSchema == null) {
      WARNING('Trying to reference "' + typeName + '" ' +
              'with neither namespace nor default schema.');
      return null;
    }
    var types = defaultSchema.types;
  } else {
    var schemaName = $Array.join($Array.slice(parts, 0, parts.length - 1), '.');
    var types = schemaRegistry.GetSchema(schemaName).types;
  }
  for (var i = 0; i < types.length; ++i) {
    if (types[i].id == typeName)
      return types[i];
  }
  return null;
}

/**
 * Takes a private class implementation |cls| and exposes a subset of its
 * methods |functions| and properties |properties| and |readonly| in a public
 * wrapper class that it returns. Within bindings code, you can access the
 * implementation from an instance of the wrapper class using
 * privates(instance).impl, and from the implementation class you can access
 * the wrapper using this.wrapper (or implInstance.wrapper if you have another
 * instance of the implementation class).
 * @param {string} name The name of the exposed wrapper class.
 * @param {Object} cls The class implementation.
 * @param {{functions: ?Array.<string>,
 *          properties: ?Array.<string>,
 *          readonly: ?Array.<string>}} exposed The names of properties on the
 *     implementation class to be exposed. |functions| represents the names of
 *     functions which should be delegated to the implementation; |properties|
 *     are gettable/settable properties and |readonly| are read-only properties.
 */
function expose(name, cls, exposed) {
  var publicClass = createClassWrapper(name, cls);

  if ('functions' in exposed) {
    $Array.forEach(exposed.functions, function(func) {
      publicClass.prototype[func] = function() {
        var impl = privates(this).impl;
        return $Function.apply(impl[func], impl, arguments);
      };
    });
  }

  if ('properties' in exposed) {
    $Array.forEach(exposed.properties, function(prop) {
      $Object.defineProperty(publicClass.prototype, prop, {
        enumerable: true,
        get: function() {
          return privates(this).impl[prop];
        },
        set: function(value) {
          var impl = privates(this).impl;
          delete impl[prop];
          impl[prop] = value;
        }
      });
    });
  }

  if ('readonly' in exposed) {
    $Array.forEach(exposed.readonly, function(readonly) {
      $Object.defineProperty(publicClass.prototype, readonly, {
        enumerable: true,
        get: function() {
          return privates(this).impl[readonly];
        },
      });
    });
  }

  return publicClass;
}

exports.forEach = forEach;
exports.loadTypeSchema = loadTypeSchema;
exports.lookup = lookup;
exports.expose = expose;
