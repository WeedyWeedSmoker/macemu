//
//  B2ScreenView.h
//  BasiliskII
//
//  Created by Jesús A. Álvarez on 09/03/2014.
//  Copyright (c) 2014 namedfork. All rights reserved.
//

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface B2ScreenView : UIView

@property (nonatomic, assign) CGSize screenSize;
@property (nonatomic, assign) CGRect screenBounds;
@property (nonatomic, readonly) NSArray<NSValue*> *videoModes;
@property (nonatomic, readonly) BOOL hasRetinaVideoMode;
@property (nonatomic, strong) IBOutletCollection(NSLayoutConstraint) NSArray<NSLayoutConstraint*> *fullScreenConstraints;
@property (nonatomic, strong) IBOutletCollection(NSLayoutConstraint) NSArray<NSLayoutConstraint*> *marginConstraints;

- (void)updateImage:(CGImageRef)newImage;

@end

extern B2ScreenView* _Nullable sharedScreenView;

NS_ASSUME_NONNULL_END
