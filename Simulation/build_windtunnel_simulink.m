% Build an editable Simulink model: torque PID + feedforward + slip-motor.
% Run windtunnel_parameters first or use defaults below.
% This is a SIMULATION ONLY; never export its output to a real V20 drive.
model='windtunnel_torque_ff';
if bdIsLoaded(model), close_system(model,0); end
new_system(model); open_system(model);
Ts=0.005;
Jm=0.10; Jfan=0.20; J=Jm+Jfan; % illustrative, replace
ratedRPM=1450; ratedPower=4000; ratedHz=50; polePairs=2;
omega0=ratedRPM*2*pi/60; Kv=40/omega0;
Ks=(ratedPower/omega0)/(2*pi*ratedHz/polePairs-omega0);
Kload=(ratedPower/omega0)/(omega0^2);
fd=0.02; M=5/0.5; wd=2*pi*fd; Kp=M*J*wd/Kv; wb=Kp*Kv/J;
Ti=10/wb; Td=3/wb; Tf=1/(10*wb); Ki=Kp/Ti; Kd=Kp*Td;
assignin('base','J',J);assignin('base','Kv',Kv);
assignin('base','Ks',Ks);assignin('base','Kload',Kload);
assignin('base','Kp',Kp);assignin('base','Ki',Ki);
assignin('base','Kd',Kd);assignin('base','Tf',Tf);
assignin('base','Ts',Ts);assignin('base','polePairs',polePairs);
% Reference can later be replaced by your quintic trajectory block.
add_block('simulink/Sources/Step',[model '/Airspeed reference'],'Position',[20 80 65 110],...
 'Time','5','Before','0','After','20');
add_block('simulink/Math Operations/Sum',[model '/Error'],'Inputs','+-',...
 'Position',[110 75 130 115]);
add_block('simulink/Continuous/PID Controller',[model '/Torque PID'],...
 'P','Kp','I','Ki','D','Kd','N','1/Tf','Position',[165 73 255 113]);
% Default demonstration uses derivative feedforward filtered by a first-order
% proper differentiator, to avoid algebraic/sampling noise amplification.
add_block('simulink/Continuous/Transfer Fcn',[model '/Acceleration filter'],...
 'Numerator','[1 0]','Denominator','[0.05 1]','Position',[145 180 255 215]);
add_block('simulink/Math Operations/Gain',[model '/Inertia feedforward'],...
 'Gain','J/Kv','Position',[290 180 360 215]);
add_block('simulink/Math Operations/Sum',[model '/Requested torque'],...
 'Inputs','+++','Position',[395 80 420 135]);
add_block('simulink/Math Operations/Gain',[model '/Load torque gain'],...
 'Gain','Kload','Position',[305 285 365 315]);
add_block('simulink/Math Operations/Product',[model '/omega abs omega'],...
 'Inputs','**','Position',[220 275 250 315]);
add_block('simulink/Math Operations/Abs',[model '/abs omega'],...
 'Position',[150 315 185 345]);
add_block('simulink/Math Operations/Sum',[model '/Mechanical net torque'],...
 'Inputs','+-','Position',[615 120 640 160]);
add_block('simulink/Math Operations/Gain',[model '/1 over J'],...
 'Gain','1/J','Position',[660 120 715 160]);
add_block('simulink/Continuous/Integrator',[model '/Rotor speed'],...
 'Position',[750 120 780 155]);
add_block('simulink/Math Operations/Gain',[model '/Kv airspeed'],...
 'Gain','Kv','Position',[810 120 860 155]);
% Motor inversion: f=(omega+requestedTorque/Ks)*p/(2*pi).
add_block('simulink/Math Operations/Gain',[model '/Torque per slip'],...
 'Gain','1/Ks','Position',[450 80 505 115]);
add_block('simulink/Math Operations/Sum',[model '/Sync speed request'],...
 'Inputs','++','Position',[535 83 555 123]);
add_block('simulink/Math Operations/Gain',[model '/Electrical Hz conversion'],...
 'Gain','polePairs/(2*pi)','Position',[575 70 680 100]);
add_block('simulink/Discontinuities/Saturation',[model '/V20 frequency limit'],...
 'UpperLimit','50','LowerLimit','0','Position',[710 70 770 100]);
add_block('simulink/Math Operations/Gain',[model '/Frequency to sync speed'],...
 'Gain','2*pi/polePairs','Position',[790 70 870 100]);
add_block('simulink/Math Operations/Sum',[model '/Slip speed'],...
 'Inputs','+-','Position',[900 75 920 110]);
add_block('simulink/Math Operations/Gain',[model '/Motor slip torque'],...
 'Gain','Ks','Position',[945 75 1005 110]);
add_block('simulink/Sinks/Scope',[model '/Airspeed scope'],...
 'Position',[930 180 970 215]);
% Connections
add_line(model,'Airspeed reference/1','Error/1','autorouting','on');
add_line(model,'Error/1','Torque PID/1','autorouting','on');
add_line(model,'Torque PID/1','Requested torque/1','autorouting','on');
add_line(model,'Airspeed reference/1','Acceleration filter/1','autorouting','on');
add_line(model,'Acceleration filter/1','Inertia feedforward/1','autorouting','on');
add_line(model,'Inertia feedforward/1','Requested torque/2','autorouting','on');
add_line(model,'Requested torque/1','Torque per slip/1','autorouting','on');
add_line(model,'Torque per slip/1','Sync speed request/1','autorouting','on');
add_line(model,'Sync speed request/1','Electrical Hz conversion/1','autorouting','on');
add_line(model,'Electrical Hz conversion/1','V20 frequency limit/1','autorouting','on');
add_line(model,'V20 frequency limit/1','Frequency to sync speed/1','autorouting','on');
add_line(model,'Frequency to sync speed/1','Slip speed/1','autorouting','on');
add_line(model,'Slip speed/1','Motor slip torque/1','autorouting','on');
add_line(model,'Motor slip torque/1','Mechanical net torque/1','autorouting','on');
add_line(model,'Mechanical net torque/1','1 over J/1','autorouting','on');
add_line(model,'1 over J/1','Rotor speed/1','autorouting','on');
add_line(model,'Rotor speed/1','Kv airspeed/1','autorouting','on');
add_line(model,'Kv airspeed/1','Error/2','autorouting','on');
add_line(model,'Kv airspeed/1','Airspeed scope/1','autorouting','on');
add_line(model,'Rotor speed/1','Sync speed request/2','autorouting','on');
add_line(model,'Rotor speed/1','Slip speed/2','autorouting','on');
add_line(model,'Rotor speed/1','omega abs omega/1','autorouting','on');
add_line(model,'Rotor speed/1','abs omega/1','autorouting','on');
add_line(model,'abs omega/1','omega abs omega/2','autorouting','on');
add_line(model,'omega abs omega/1','Load torque gain/1','autorouting','on');
add_line(model,'Load torque gain/1','Mechanical net torque/2','autorouting','on');
add_line(model,'Load torque gain/1','Requested torque/3','autorouting','on');
set_param(model,'StopTime','50','Solver','ode45');
save_system(model,[model '.slx']);
fprintf('Created %s.slx. This is an illustrative continuous model, not V20 commissioning software.\n',model);
