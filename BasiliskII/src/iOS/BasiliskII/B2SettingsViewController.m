//
//  B2SettingsViewController.m
//  BasiliskII
//
//  Created by Jesús A. Álvarez on 05/07/2015.
//  Copyright (c) 2015 namedfork. All rights reserved.
//

#import "B2SettingsViewController.h"
#import "B2AppDelegate.h"

@interface B2SettingsViewController () <UISplitViewControllerDelegate>

@end

@implementation B2SettingsViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.splitViewController.delegate = self;
    self.splitViewController.preferredDisplayMode = UISplitViewControllerDisplayModeAllVisible;
}

- (UISplitViewController *)splitViewController {
    return self.childViewControllers.firstObject;
}

- (BOOL)splitViewController:(UISplitViewController *)splitViewController collapseSecondaryViewController:(UIViewController *)secondaryViewController ontoPrimaryViewController:(UIViewController *)primaryViewController {
    return YES;
}

@end
