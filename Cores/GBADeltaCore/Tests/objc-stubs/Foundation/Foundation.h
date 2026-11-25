// Minimal Foundation stand-in: just enough to type-check GBADeltaCore's ObjC on Linux (clang -fsyntax-only).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#define nil ((id)0)
typedef long NSInteger; typedef unsigned long NSUInteger; typedef double NSTimeInterval;
#define NSNotFound ((NSInteger)0x7fffffffffffffffL)
typedef struct { NSUInteger location, length; } NSRange;
#ifdef __cplusplus
#define FOUNDATION_EXPORT extern "C"
#else
#define FOUNDATION_EXPORT extern
#endif
#define NS_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define NS_ASSUME_NONNULL_END _Pragma("clang assume_nonnull end")
#define NS_HEADER_AUDIT_BEGIN(...) NS_ASSUME_NONNULL_BEGIN
#define NS_HEADER_AUDIT_END(...) NS_ASSUME_NONNULL_END
#define NS_ENUM(T, N) enum N : T N; enum N : T
#define NS_REFINED_FOR_SWIFT __attribute__((swift_private))
#define NS_SWIFT_NAME(x) __attribute__((swift_name(#x)))
#define NS_TYPED_EXTENSIBLE_ENUM __attribute__((swift_wrapper(struct)))
#define API_DEPRECATED(...)
@class NSString; @class NSDictionary<K, V>; @class NSArray<T>;
@protocol NSObject
- (BOOL)isEqual:(id)object;
@end
__attribute__((objc_root_class))
@interface NSObject <NSObject> { Class isa; }
+ (instancetype)alloc; - (instancetype)init; + (instancetype)new;
@end
@interface NSString : NSObject
+ (instancetype)stringWithUTF8String:(const char *)s;
@property (readonly) NSUInteger length;
@property (readonly) const char *UTF8String;
- (BOOL)isEqualToString:(NSString *)s;
- (NSArray<NSString *> *)componentsSeparatedByString:(NSString *)s;
- (NSRange)rangeOfCharacterFromSet:(id)set;
@end
@interface NSNumber : NSObject
+ (NSNumber *)numberWithInteger:(NSInteger)v; + (NSNumber *)numberWithLong:(long)v; + (NSNumber *)numberWithInt:(int)v;
@end
@interface NSArray<T> : NSObject
+ (instancetype)arrayWithObjects:(const T _Nonnull [_Nullable])objects count:(NSUInteger)cnt;
- (NSUInteger)countByEnumeratingWithState:(void *)state objects:(id __unsafe_unretained _Nullable [_Nonnull])buffer count:(NSUInteger)len;
@end
@interface NSDictionary<K, V> : NSObject
+ (instancetype)dictionaryWithObjects:(const V _Nonnull [_Nullable])objects forKeys:(const K _Nonnull [_Nullable])keys count:(NSUInteger)cnt;
@end
@interface NSURL : NSObject
@property (readonly) const char *fileSystemRepresentation;
@property (readonly, copy) NSString *lastPathComponent;
@end
@interface NSData : NSObject
+ (instancetype)dataWithBytesNoCopy:(void *)bytes length:(NSUInteger)length freeWhenDone:(BOOL)b;
@end
@interface NSBundle : NSObject
@end
@interface NSCharacterSet : NSObject
+ (NSCharacterSet *)hexadecimalCharacterSet;
@property (readonly, copy) NSCharacterSet *invertedSet;
@end
@interface NSMutableCharacterSet : NSCharacterSet
+ (NSMutableCharacterSet *)hexadecimalCharacterSet;
- (void)addCharactersInString:(NSString *)s;
@end
typedef NSString *NSNotificationName;
@interface NSNotificationCenter : NSObject
@property (class, readonly, strong) NSNotificationCenter *defaultCenter;
- (void)postNotificationName:(NSNotificationName)name object:(id)obj userInfo:(NSDictionary *)info;
- (void)postNotificationName:(NSNotificationName)name object:(id)obj;
@end
@class NSNetService;
@protocol NSNetServiceDelegate <NSObject>
@optional
- (void)netService:(NSNetService *)sender didNotPublish:(NSDictionary<NSString *, NSNumber *> *)errorDict __attribute__((deprecated));
@end
__attribute__((deprecated))
@interface NSNetService : NSObject
- (instancetype)initWithDomain:(NSString *)domain type:(NSString *)type name:(NSString *)name port:(int)port;
@property (weak) id<NSNetServiceDelegate> delegate;
- (void)publish; - (void)stop;
@end
FOUNDATION_EXPORT void NSLog(NSString *fmt, ...);
typedef struct dispatch_queue_s *dispatch_queue_t; typedef long dispatch_once_t;
#ifdef __cplusplus
extern "C" {
#endif
dispatch_queue_t dispatch_get_main_queue(void);
void dispatch_async(dispatch_queue_t q, void (^block)(void));
void dispatch_once(dispatch_once_t *p, void (^block)(void));
#ifdef __cplusplus
}
#endif
