// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSION_H_
#define EXTENSIONS_COMMON_EXTENSION_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/auto_reset.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "base/version.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/hashed_extension_id.h"
#include "extensions/common/install_warning.h"
#include "extensions/common/manifest.h"
#include "extensions/common/url_pattern_set.h"
#include "url/gurl.h"

#if !BUILDFLAG(ENABLE_EXTENSIONS)
#error "Extensions must be enabled"
#endif

namespace base {
class DictionaryValue;
class Version;
}

namespace extensions {
class PermissionSet;
class PermissionsData;
class PermissionsParser;

// Represents a Chrome extension.
// Once created, an Extension object is immutable, with the exception of its
// RuntimeData. This makes it safe to use on any thread, since access to the
// RuntimeData is protected by a lock.
class Extension : public base::RefCountedThreadSafe<Extension> {
 public:
  // Do not renumber or reorder these values, as they are stored on-disk in the
  // user's preferences.
  enum State {
    DISABLED = 0,
    ENABLED = 1,

    // DEPRECATED. External uninstallation bits are now stored directly in
    // the ExtensionPrefs. See https://crbug.com/795026.
    // An external extension that the user uninstalled. We should not reinstall
    // such extensions on startup.
    DEPRECATED_EXTERNAL_EXTENSION_UNINSTALLED = 2,

    // DEPRECATED: Special state for component extensions.
    // ENABLED_COMPONENT_DEPRECATED = 3,

    // Do not add more values. State is being removed.
    // https://crbug.com/794205.
    NUM_STATES = 4,
  };

  // A base class for parsed manifest data that APIs want to store on
  // the extension. Related to base::SupportsUserData, but with an immutable
  // thread-safe interface to match Extension.
  struct ManifestData {
    virtual ~ManifestData() {}
  };

  // Do not change the order of entries or remove entries in this list
  // as this is used in UMA_HISTOGRAM_ENUMERATIONs about extensions.
  enum InitFromValueFlags {
    NO_FLAGS = 0,

    // Usually, the id of an extension is generated by the "key" property of
    // its manifest, but if |REQUIRE_KEY| is not set, a temporary ID will be
    // generated based on the path.
    REQUIRE_KEY = 1 << 0,

    // Requires the extension to have an up-to-date manifest version.
    // Typically, we'll support multiple manifest versions during a version
    // transition. This flag signals that we want to require the most modern
    // manifest version that Chrome understands.
    REQUIRE_MODERN_MANIFEST_VERSION = 1 << 1,

    // |ALLOW_FILE_ACCESS| indicates that the user is allowing this extension
    // to have file access. If it's not present, then permissions and content
    // scripts that match file:/// URLs will be filtered out.
    ALLOW_FILE_ACCESS = 1 << 2,

    // |FROM_WEBSTORE| indicates that the extension was installed from the
    // Chrome Web Store.
    FROM_WEBSTORE = 1 << 3,

    // |FROM_BOOKMARK| indicates the extension is a bookmark app which has been
    // generated from a web page. Bookmark apps have no permissions or extent
    // and launch the web page they are created from when run.
    FROM_BOOKMARK = 1 << 4,

    // |FOLLOW_SYMLINKS_ANYWHERE| means that resources can be symlinks to
    // anywhere in the filesystem, rather than being restricted to the
    // extension directory.
    FOLLOW_SYMLINKS_ANYWHERE = 1 << 5,

    // |ERROR_ON_PRIVATE_KEY| means that private keys inside an
    // extension should be errors rather than warnings.
    ERROR_ON_PRIVATE_KEY = 1 << 6,

    // |WAS_INSTALLED_BY_DEFAULT| installed by default when the profile was
    // created.
    WAS_INSTALLED_BY_DEFAULT = 1 << 7,

    // Unused - was part of an abandoned experiment.
    REQUIRE_PERMISSIONS_CONSENT = 1 << 8,

    // Unused - this flag has been moved to ExtensionPrefs.
    IS_EPHEMERAL = 1 << 9,

    // |WAS_INSTALLED_BY_OEM| installed by an OEM (e.g on Chrome OS) and should
    // be placed in a special OEM folder in the App Launcher. Note: OEM apps are
    // also installed by Default (i.e. WAS_INSTALLED_BY_DEFAULT is also true).
    WAS_INSTALLED_BY_OEM = 1 << 10,

    // DEPRECATED: WAS_INSTALLED_BY_CUSTODIAN is now stored as a pref instead.
    // WAS_INSTALLED_BY_CUSTODIAN = 1 << 11,

    // |MAY_BE_UNTRUSTED| indicates that this extension came from a potentially
    // unsafe source (e.g., sideloaded from a local CRX file via the Windows
    // registry). Such extensions may be subjected to additional constraints
    // before they are fully installed and enabled.
    MAY_BE_UNTRUSTED = 1 << 12,

    // |FOR_LOGIN_SCREEN| means that this extension was force-installed through
    // policy for the login screen. Extensions created with this flag will have
    // type |TYPE_LOGIN_SCREEN_EXTENSION| (with limited API capabilities)
    // instead of the usual |TYPE_EXTENSION|.
    FOR_LOGIN_SCREEN = 1 << 13,

    // |WITHHOLD_PERMISSIONS| indicates that on installation the user indicated
    // for permissions to be withheld from the extension by default.
    WITHHOLD_PERMISSIONS = 1 << 14,

    // When adding new flags, make sure to update kInitFromValueFlagBits.
  };

  // This is the highest bit index of the flags defined above.
  static const int kInitFromValueFlagBits;

  static scoped_refptr<Extension> Create(const base::FilePath& path,
                                         Manifest::Location location,
                                         const base::DictionaryValue& value,
                                         int flags,
                                         std::string* error);

  // In a few special circumstances, we want to create an Extension and give it
  // an explicit id. Most consumers should just use the other Create() method.
  static scoped_refptr<Extension> Create(const base::FilePath& path,
                                         Manifest::Location location,
                                         const base::DictionaryValue& value,
                                         int flags,
                                         const ExtensionId& explicit_id,
                                         std::string* error);

  // Valid schemes for web extent URLPatterns.
  static const int kValidWebExtentSchemes;

  // Valid schemes for bookmark app installs by the user.
  static const int kValidBookmarkAppSchemes;

  // Valid schemes for host permission URLPatterns.
  static const int kValidHostPermissionSchemes;

  // The mimetype used for extensions.
  static const char kMimeType[];

  // See Type definition in Manifest.
  Manifest::Type GetType() const;

  // Returns an absolute url to a resource inside of an extension. The
  // |extension_url| argument should be the url() from an Extension object. The
  // |relative_path| can be untrusted user input. The returned URL will either
  // be invalid() or a child of |extension_url|.
  // NOTE: Static so that it can be used from multiple threads.
  static GURL GetResourceURL(const GURL& extension_url,
                             const std::string& relative_path);
  GURL GetResourceURL(const std::string& relative_path) const {
    return GetResourceURL(url(), relative_path);
  }

  // Returns true if the resource matches a pattern in the pattern_set.
  bool ResourceMatches(const URLPatternSet& pattern_set,
                       const std::string& resource) const;

  // Returns an extension resource object. |relative_path| should be UTF8
  // encoded.
  ExtensionResource GetResource(base::StringPiece relative_path) const;

  // As above, but with |relative_path| following the file system's encoding.
  ExtensionResource GetResource(const base::FilePath& relative_path) const;

  // |input| is expected to be the text of an rsa public or private key. It
  // tolerates the presence or absence of bracking header/footer like this:
  //     -----(BEGIN|END) [RSA PUBLIC/PRIVATE] KEY-----
  // and may contain newlines.
  static bool ParsePEMKeyBytes(const std::string& input, std::string* output);

  // Does a simple base64 encoding of |input| into |output|.
  static bool ProducePEM(const std::string& input, std::string* output);

  // Expects base64 encoded |input| and formats into |output| including
  // the appropriate header & footer.
  static bool FormatPEMForFileOutput(const std::string& input,
                                     std::string* output,
                                     bool is_public);

  // Returns the base extension url for a given |extension_id|.
  static GURL GetBaseURLFromExtensionId(const ExtensionId& extension_id);

  // Returns true if this extension or app includes areas within |origin|.
  bool OverlapsWithOrigin(const GURL& origin) const;

  // Returns true if the extension requires a valid ordinal for sorting, e.g.,
  // for displaying in a launcher or new tab page.
  bool RequiresSortOrdinal() const;

  // TODO(devlin): The core Extension class shouldn't be responsible for these
  // ShouldDisplay/ShouldExpose style functions; it doesn't know about the NTP,
  // Management API, etc.

  // Returns true if the extension should be displayed in the app launcher.
  bool ShouldDisplayInAppLauncher() const;

  // Returns true if the extension should be displayed in the browser NTP.
  bool ShouldDisplayInNewTabPage() const;

  // Returns true if the extension should be exposed via the chrome.management
  // API.
  bool ShouldExposeViaManagementAPI() const;

  // Get the manifest data associated with the key, or NULL if there is none.
  // Can only be called after InitValue is finished.
  ManifestData* GetManifestData(const std::string& key) const;

  // Sets |data| to be associated with the key.
  // Can only be called before InitValue is finished. Not thread-safe;
  // all SetManifestData calls should be on only one thread.
  void SetManifestData(const std::string& key,
                       std::unique_ptr<ManifestData> data);

  // Accessors:

  const base::FilePath& path() const { return path_; }
  const GURL& url() const { return extension_url_; }
  Manifest::Location location() const;
  const ExtensionId& id() const;
  const HashedExtensionId& hashed_id() const;
  const base::Version& version() const { return version_; }
  const std::string& version_name() const { return version_name_; }
  std::string VersionString() const;
  std::string DifferentialFingerprint() const;
  std::string GetVersionForDisplay() const;
  const std::string& name() const { return display_name_; }
  const std::string& short_name() const { return short_name_; }
  const std::string& non_localized_name() const { return non_localized_name_; }
  // Base64-encoded version of the key used to sign this extension.
  // In pseudocode, returns
  // base::Base64Encode(RSAPrivateKey(pem_file).ExportPublicKey()).
  const std::string& public_key() const { return public_key_; }
  const std::string& description() const { return description_; }
  int manifest_version() const { return manifest_version_; }
  bool converted_from_user_script() const {
    return converted_from_user_script_;
  }
  PermissionsParser* permissions_parser() { return permissions_parser_.get(); }
  const PermissionsParser* permissions_parser() const {
    return permissions_parser_.get();
  }

  const PermissionsData* permissions_data() const {
    return permissions_data_.get();
  }

  // Appends |new_warning[s]| to install_warnings_.
  void AddInstallWarning(InstallWarning new_warning);
  void AddInstallWarnings(std::vector<InstallWarning> new_warnings);
  const std::vector<InstallWarning>& install_warnings() const {
    return install_warnings_;
  }
  const extensions::Manifest* manifest() const {
    return manifest_.get();
  }
  bool wants_file_access() const { return wants_file_access_; }
  // TODO(rdevlin.cronin): This is needed for ContentScriptsHandler, and should
  // be moved out as part of crbug.com/159265. This should not be used anywhere
  // else.
  void set_wants_file_access(bool wants_file_access) {
    wants_file_access_ = wants_file_access;
  }
  int creation_flags() const { return creation_flags_; }
  bool from_webstore() const { return (creation_flags_ & FROM_WEBSTORE) != 0; }
  bool from_bookmark() const { return (creation_flags_ & FROM_BOOKMARK) != 0; }
  bool may_be_untrusted() const {
    return (creation_flags_ & MAY_BE_UNTRUSTED) != 0;
  }
  bool was_installed_by_default() const {
    return (creation_flags_ & WAS_INSTALLED_BY_DEFAULT) != 0;
  }
  bool was_installed_by_oem() const {
    return (creation_flags_ & WAS_INSTALLED_BY_OEM) != 0;
  }

  // Type-related queries. These are all mutually exclusive.
  //
  // The differences between the types of Extension are documented here:
  // https://chromium.googlesource.com/chromium/src/+/HEAD/extensions/docs/extension_and_app_types.md
  bool is_platform_app() const;         // aka "V2 app", "V2 packaged app"
  bool is_hosted_app() const;           // Hosted app (or bookmark app)
  bool is_legacy_packaged_app() const;  // aka "V1 packaged app"
  bool is_extension() const;            // Regular browser extension, not an app
  bool is_shared_module() const;        // Shared module
  bool is_theme() const;                // Theme
  bool is_login_screen_extension() const;  // Extension on login screen.

  // True if this is a platform app, hosted app, or legacy packaged app.
  bool is_app() const;

  void AddWebExtentPattern(const URLPattern& pattern);
  const URLPatternSet& web_extent() const { return extent_; }

 private:
  friend class base::RefCountedThreadSafe<Extension>;

  // Chooses the extension ID for an extension based on a variety of criteria.
  // The chosen ID will be set in |manifest|.
  static bool InitExtensionID(extensions::Manifest* manifest,
                              const base::FilePath& path,
                              const ExtensionId& explicit_id,
                              int creation_flags,
                              base::string16* error);

  Extension(const base::FilePath& path,
            std::unique_ptr<extensions::Manifest> manifest);
  virtual ~Extension();

  // Initialize the extension from a parsed manifest.
  // TODO(aa): Rename to just Init()? There's no Value here anymore.
  // TODO(aa): It is really weird the way this class essentially contains a copy
  // of the underlying DictionaryValue in its members. We should decide to
  // either wrap the DictionaryValue and go with that only, or we should parse
  // into strong types and discard the value. But doing both is bad.
  bool InitFromValue(int flags, base::string16* error);

  // The following are helpers for InitFromValue to load various features of the
  // extension from the manifest.

  bool LoadRequiredFeatures(base::string16* error);
  bool LoadName(base::string16* error);
  bool LoadVersion(base::string16* error);

  bool LoadAppFeatures(base::string16* error);
  bool LoadExtent(const char* key,
                  URLPatternSet* extent,
                  const char* list_error,
                  const char* value_error,
                  base::string16* error);

  bool LoadSharedFeatures(base::string16* error);
  bool LoadDescription(base::string16* error);
  bool LoadManifestVersion(base::string16* error);
  bool LoadShortName(base::string16* error);

  bool CheckMinimumChromeVersion(base::string16* error) const;

  // The extension's human-readable name. Name is used for display purpose. It
  // might be wrapped with unicode bidi control characters so that it is
  // displayed correctly in RTL context.
  // NOTE: Name is UTF-8 and may contain non-ascii characters.
  std::string display_name_;

  // A non-localized version of the extension's name. This is useful for
  // debug output.
  std::string non_localized_name_;

  // A short version of the extension's name. This can be used as an alternative
  // to the name where there is insufficient space to display the full name. If
  // an extension has not explicitly specified a short name, the value of this
  // member variable will be the full name rather than an empty string.
  std::string short_name_;

  // The version of this extension's manifest. We increase the manifest
  // version when making breaking changes to the extension system.
  // Version 1 was the first manifest version (implied by a lack of a
  // manifest_version attribute in the extension's manifest). We initialize
  // this member variable to 0 to distinguish the "uninitialized" case from
  // the case when we know the manifest version actually is 1.
  int manifest_version_;

  // The absolute path to the directory the extension is stored in.
  base::FilePath path_;

  // Defines the set of URLs in the extension's web content.
  URLPatternSet extent_;

  // The parser for the manifest's permissions. This is NULL anytime not during
  // initialization.
  // TODO(rdevlin.cronin): This doesn't really belong here.
  std::unique_ptr<PermissionsParser> permissions_parser_;

  // The active permissions for the extension.
  std::unique_ptr<PermissionsData> permissions_data_;

  // Any warnings that occurred when trying to create/parse the extension.
  std::vector<InstallWarning> install_warnings_;

  // The base extension url for the extension.
  GURL extension_url_;

  // The extension's version.
  base::Version version_;

  // The extension's user visible version name.
  std::string version_name_;

  // An optional longer description of the extension.
  std::string description_;

  // True if the extension was generated from a user script. (We show slightly
  // different UI if so).
  bool converted_from_user_script_;

  // The public key used to sign the contents of the crx package.
  std::string public_key_;

  // The manifest from which this extension was created.
  std::unique_ptr<Manifest> manifest_;

  // Stored parsed manifest data.
  using ManifestDataMap = std::map<std::string, std::unique_ptr<ManifestData>>;
  ManifestDataMap manifest_data_;

  // Set to true at the end of InitValue when initialization is finished.
  bool finished_parsing_manifest_;

  // Ensures that any call to GetManifestData() prior to finishing
  // initialization happens from the same thread (this can happen when certain
  // parts of the initialization process need information from previous parts).
  base::ThreadChecker thread_checker_;

  // Should this app be shown in the app launcher.
  bool display_in_launcher_;

  // Should this app be shown in the browser New Tab Page.
  bool display_in_new_tab_page_;

  // Whether the extension has host permissions or user script patterns that
  // imply access to file:/// scheme URLs (the user may not have actually
  // granted it that access).
  bool wants_file_access_;

  // The flags that were passed to InitFromValue.
  int creation_flags_;

  DISALLOW_COPY_AND_ASSIGN(Extension);
};

typedef std::vector<scoped_refptr<const Extension> > ExtensionList;

// Handy struct to pass core extension info around.
struct ExtensionInfo {
  ExtensionInfo(const base::DictionaryValue* manifest,
                const ExtensionId& id,
                const base::FilePath& path,
                Manifest::Location location);
  ~ExtensionInfo();

  // Note: This may be null (e.g. for unpacked extensions retrieved from the
  // Preferences file).
  std::unique_ptr<base::DictionaryValue> extension_manifest;

  ExtensionId extension_id;
  base::FilePath extension_path;
  Manifest::Location extension_location;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionInfo);
};

// The details sent for EXTENSION_PERMISSIONS_UPDATED notifications.
struct UpdatedExtensionPermissionsInfo {
  enum Reason {
    ADDED,    // The permissions were added to the extension.
    REMOVED,  // The permissions were removed from the extension.
    POLICY,   // The policy that affects permissions was updated.
  };

  Reason reason;

  // The extension who's permissions have changed.
  const Extension* extension;

  // The permissions that have changed. For Reason::ADDED, this would contain
  // only the permissions that have added, and for Reason::REMOVED, this would
  // only contain the removed permissions.
  const PermissionSet& permissions;

  UpdatedExtensionPermissionsInfo(const Extension* extension,
                                  const PermissionSet& permissions,
                                  Reason reason);
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_EXTENSION_H_
